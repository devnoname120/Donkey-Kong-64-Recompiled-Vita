# Vita renderer architecture experiments

These are **diagnostic prototypes**, not a production renderer or a faster normal-game package. Read [the architecture proposal](../../docs/VITA_RENDERER_ARCHITECTURE.md) and [the recorded measurements](measurements.json) before interpreting the results.

The retained-frame experiment bypasses command decoding and CPU vertex work by replaying an already captured frame. The native control is a single-pipeline synthetic workload. Neither is a live-game FPS benchmark. `tools/compare_vita_fps.py` rejects captures marked `architecture_probe`.

## Prerequisites and safety

Run commands from this repository's root in the existing checkout. The supplied builder requires the measured local image `dk64-vita-fps-final-noscratch` with image ID `sha256:cac4379657612cad033a8a0b47026d394e4049a792a684995a45bab0f09719eb`. That image contains the pinned vitaGL/vitaShaRK combination, the viewport correction, `NO_SPLASHSCREEN=1`, `READBACKS_SPEEDHACK=1`, `USE_SCRATCH_MEMORY=0`, and disabled shader/error logging. A different dependency build needs its own verified baseline; do not silently combine its results with this record.

The local ROM and generated recompiler sources must already exist. ROMs, Sony SDK contents, raw dumps, game resources, captured images and private saves are not supplied here. Python 3.10+, Docker with linux/amd64 execution, and the project's existing tools are required.

Before device work, inspect current processes and `build/fps-autobench/device.lock`. Use only one device job at a time. The scripts temporarily replace the executable for `DK64RE001`, require it to match the explicit `--original` package, retain a verified backup, and restore it. They use private benchmark saves, not the normal save directory. Never restore over an unrecognized executable or interrupt another active owner. The scripts do not send global controller inputs.

The expected normal package for the recorded experiments is `build/releases/DK64Recompiled-e97b563.vpk`; future normal installations change that recovery baseline. Read its eboot and verify the device before using it. The default device is `192.168.1.146`, FTP 1337 and VitaCompanion 1338; runners accept `--host`.

## Build

```sh
python3 tools/vita_architecture/build_variant.py replay-01 --mode replay
python3 tools/vita_architecture/build_variant.py native-01 --mode native
python3 tools/vita_architecture/build_variant.py coarse-01 --mode coarse
```

Outputs are immutable named artifacts in `build/vita-architecture/packages/`. Use a new artifact name for a rebuild. `--output` can select another directory beneath `build/`.

The builder does not edit live RT64 or frontend files. It produces contextual source overlays, verifies replacement sites, excludes the unrelated audio follow-up by mounting committed versions of shared frontend files, removes affected object files and fingerprints sources/configuration. The precomputed control was rebuilt with this public builder and tested on the physical Vita. The shader/image experiment uses the same source bytes as the recorded frozen-frame probes.

This is a development harness, not an arbitrary-version compatibility layer. A later change to RT64/frontend structure may invalidate an overlay and should fail rather than silently omit instrumentation. Legitimate new changes to shared frontend files must be included deliberately instead of being masked by the committed-file overlay strategy.

## Actual-frame replay and source reuse

```sh
python3 tools/vita_architecture/run_replay.py \
  --package build/vita-architecture/packages/replay-01.vpk \
  --original build/releases/DK64Recompiled-e97b563.vpk \
  --output build/vita-architecture/rap-01

python3 tools/vita_architecture/run_replay.py \
  --package build/vita-architecture/packages/replay-01.vpk \
  --original build/releases/DK64Recompiled-e97b563.vpk \
  --output build/vita-architecture/world-01 --scenario world
```

The executable records source reuse in a bounded observation window, then holds one task on its graphics thread while testing its captured packet stream. This intentionally pauses normal progress inside the private diagnostic run. It does not change the normal game's code or timing.

Replay modes are:

| Mode | Workload | Output requirement |
| --- | --- | --- |
| 0 | Frozen packets with streamed transformed vertices | Exact captured RGBA image |
| 1 | Same packets with resident vertex buffers | Exact captured RGBA image |
| 2 | Resident vertices and a quarter-area viewport | Deliberately non-equivalent diagnostic |
| 3 | Resident vertices and untextured fragments | Deliberately non-equivalent diagnostic |
| 4 | Resident vertices with constant sampler descriptors | Exact captured RGBA image |

Each mode has warmup and 24 measured trials, with rotating order. `submit_us` is CPU submission wall time; `complete_us` includes completion through `glFinish`. Shader generation/compilation, resource observation and image checks are outside the timed section. Exact comparisons occur at the checked first/last trials of modes 0/1/4 and after restoring the ordinary path. The runner retains the run-specific architecture JSON and an image witness in addition to ordinary lifecycle/restoration/save receipts.

The capture refuses dependencies it cannot replay safely, including framebuffer-backed texture sampling. This restriction is not an implementation of general framebuffer dependency replay. `architecture_specialization.h` is intentionally aggressive, frozen-descriptor specialization; it is not a bounded production material cache.

## Native submission control

```sh
python3 tools/vita_architecture/run_native.py \
  --package build/vita-architecture/packages/native-01.vpk \
  --original build/releases/DK64Recompiled-e97b563.vpk \
  --output build/vita-architecture/native-run-01
```

The control uses 170 draws, 11,220 vertex references, 66 unique resident vertices, one texture and one program pair. It rotates streaming GL, resident GL, direct GXM and precomputed GXM modes through 14 trials. Two matrix/tint phases change the image; all timed trials compare exact output against their GL reference.

Precomputed submission includes uniform-buffer copying but not real game animation or an arbitrary material graph. A single context is used; precomputed states are unset before returning to GL, and completion is observed before changing/freeing referenced data. The test serializes GPU completion between trials. It does **not** validate overlapping in-flight epoch management, deferred command-list reuse or a complete native backend.

## Host checks and evidence

`test_resources.cpp` checks exact source snapshots, address/length identity, retention caps and sampler substitution invariants against the real RT64 shader generator. A tested command in the existing host image is:

```sh
docker run --rm --platform linux/amd64 -v "$PWD:/project" -w /project \
  -e ASAN_OPTIONS=detect_leaks=0 dk64-host-probe sh -c '
  g++ -std=c++17 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -msse4.1 -DHLSL_CPU -DRT64_FAST -Ilib/rt64/src -Ilib/rt64/src/contrib \
    -Ilib/rt64/src/contrib/hlslpp/include \
    tools/vita_architecture/test_resources.cpp lib/rt64/src/fast/rt64_fast_shader.cpp \
    -o build/vita-architecture/helper-checks && build/vita-architecture/helper-checks'

PYTHONPATH=tools python3 -m unittest \
  tools/test_vita_fps_benchmark.py tools/test_compare_vita_fps.py
```

The committed measurement file preserves run IDs, distributions, clocks, image-equivalence flags, source reuse counts and package/evidence hashes. Full local receipts are in `build/architecture-pass4-20260908/`. The second specialized-world repeat lost host supervision after launch; its complete capture and shutdown receipt were retrieved before guarded recovery. It is not another observed guest stall. Historical no-export stalls remain unresolved.

A completed measurement file is not sufficient evidence of a clean run. Require shutdown where applicable, no new core dump, unchanged normal saves and verified executable restoration. Preserve failed attempts and collect newly created dumps privately; do not delete remote dumps or publish their memory contents.
