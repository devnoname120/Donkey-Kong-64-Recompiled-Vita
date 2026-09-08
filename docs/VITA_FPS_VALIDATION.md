# Physical Vita FPS validation

The subsequent [second performance pass](VITA_FPS_PASS2_VALIDATION.md) records
triangle grouping, exact NEON texture-cache comparisons, and a correction to the
cached dependency image's scratch-buffer configuration. Consult that correction
before treating an older build as proof that scratch allocation was disabled.

## Scope

This pass measures new game-loop iterations and CPU graphics-task latency on the
physical Vita. It does not use the presentation counter as game FPS, promise
60 newly rendered frames per second, or change the game's lag/deadline logic.
Resolution, texture filtering, draw order, audio rate, clock requests and the
synchronous depth-query bridge remain unchanged.

**Crash-evidence correction:** the original benchmark wrote its capture before
calling `sceKernelExitProcess()` while the graphics worker was still active.
All 20 new hardware dumps retrieved on September 7 have the same GXM teardown
fault, including both reference and optimized runs. Their saved timings describe
the pre-exit workload, not successful application shutdown. A completion JSON
and an executable-restoration receipt alone did not detect this failure. See
[benchmark crash investigation](VITA_BENCHMARK_CRASH_VALIDATION.md). The repaired
runner checks runtime shutdown and new coredumps independently of capture validity.

The baseline is game `34b4d0f`, RT64 `df49d8f` and runtime `9a6a804`. The original
quiet executable has SHA-256
`5e7395b3f5b3231f2ee886282633c8abc8ed45cffb3c62b7558efbc7f856b755`.
All accepted comparisons below used actual clocks of 500 MHz CPU, 222 MHz GPU,
222 MHz bus and 166 MHz GPU crossbar. The pre-existing 500 MHz CPU setting was
retained, not introduced as an optimization.

## Changes

The reusable RT64 path now avoids four categories of redundant CPU work:

1. Triangle preparation reuses its scalar draw state, texture references and
   vertex allocation. Texture-use decoding is cached by the exact combiner and
   other-mode words. Retained draws still own independent vertices and immutable
   texture snapshots; TMEM, tile and palette changes remain authoritative.
2. A texture snapshot request only breaks a pending batch when a resident image,
   an overlapping pending color target, or an unknown backend could satisfy it.
   Ordinary RAM texture loads do not require that barrier. The metadata query
   defaults conservatively to true for other backends.
3. Checked, word-swapped RDRAM accessors are available to the optimizer inline.
   Their bounds, unaligned access behavior, arithmetic and exceptions are not
   replaced with unchecked accesses or altered precision.
4. GPU-owned byte ranges recognize an already covered equal-start interval
   instead of deleting and allocating the same map node again. A containing
   interval also avoids repeating the ownership lookup for every scissored row.
   CPU writes still remove ownership, and partial/overlapping images retain the
   original range-union semantics.

The covered-range regression produced 1,000 allocations for 1,000 identical
updates before the correction and zero afterward. Randomized union comparisons
also cover gaps, adjacency, overlap and addresses beyond the 32-bit boundary.
This is allocation evidence, not a claimed gameplay FPS multiplier.

## Measurements

The initial three changes were compared twice before the final byte-range
optimization. The Rap case uses exactly one game record and graphics record for
each timer value 120 through 659 in map 76, mode 2, cutscene 7. Both runs start at
position `(59, 5, 145)` and use identical copied save files.

| Rap comparison | Reference game FPS | Candidate game FPS | Reference mean graphics task | Candidate mean graphics task |
| --- | ---: | ---: | ---: | ---: |
| First matched run | 16.809 | 17.899 | 45.791 ms | 41.759 ms |
| Repeated matched run | 16.834 | 18.034 | 45.763 ms | 41.361 ms |

These runs establish a roughly 6.5-7.1% increase in game-loop throughput and an
8.8-9.6% reduction in graphics-task latency for that particular crowded segment.
They do not establish the same gain in every scene. Local receipts:
`build/fps-autobench/compare-inline-rap-01.json` and
`build/fps-autobench/compare-inline-rap-02.json`.

For the initial fixed free-roam comparison, the candidate reduced mean graphics
task time from 39.763 to 37.298 ms, but measured game FPS was 17.653 versus 17.705.
That 0.29% throughput difference is not convincing evidence of a gameplay FPS
gain. Rendering cost and end-to-end frame production must be reported separately.
The stationary window contains 80 observations after 30 warmup iterations, at
position `(2790.549805, 500, 2144.711670)` in map 34. Evidence:
`build/fps-autobench/compare-world-01.json`.

Final byte-range candidate comparisons are recorded separately from those
initial trials in `build/fps-autobench/range-*/`.

## Automation and measurement boundaries

`DK64_VITA_BENCHMARK=ON` builds a quiet benchmark application using
`ux0:data/dk64recompiled-benchmark`, with a copied ROM and private saves. Its
`attract` fixture leaves the intro's controller input neutral. The `world`
fixture enters the saved game and then advances idle, camera and movement phases
by observed game-loop progress, not fixed wall-time movement. Unexpected map,
mode, cutscene, automatic movement, pause or frame rollback invalidates a run.
The navigation fixture is representative validation, not normal progression or
a complete playthrough. An early longer movement route entered an automatic
transition; its capture was rejected and the measured route was shortened.

The recorder stores bounded in-memory events. CSV output and completion metadata
are written after measurement. Game-loop, graphics-task and presentation-request
counts are distinct. The later benchmark also counts completed `vglSwapBuffers`
calls separately; that is not a count of physical panel refreshes. Graphics-task
wall time includes CPU work, preemption and any waits encountered in the backend;
it is not isolated GPU execution time. The physical thread-info counters did not
supply useful per-worker running-time values, so no utilization claim is based
on them.

Detailed `RT64_FAST_PROFILE` stage instrumentation materially perturbs small,
frequently called operations. Its output is used for investigation, not the FPS
comparison tables. Timing candidates are built with that option **compiled out**,
not merely configured with a zero sampling rate. `compare_vita_fps.py` rejects
sampled captures, mismatched clocks, different saves, missing scene coverage and
different free-roam starting positions. New completion receipts also identify
whether stage instrumentation was compiled into the executable.

The runner uses VitaCompanion 1.06 on TCP 1338 and FTP on 1337. It verifies the
normal executable before temporarily replacing it, retains a unique original
backup, confirms the benchmark start receipt, retrieves the bounded capture, and
waits for the runtime-join receipt before restoring the original executable with
a readback check. The benchmark requests `ultramodern::quit()` instead of unloading
driver modules while its renderer is still running. The normal runtime finishes
the current graphics operation, shuts down the renderer, joins its event workers,
and returns to the frontend before process exit. Normal saves are compared
before and after each completed run. The SFO and installed application identity
are not replaced: the benchmark uses the existing `DK64RE001` launcher but its
own data directory. A successful launch-command reply alone is not evidence that
the application ran.

The runner inventories `ux0:data/psp2core-*` before and after the session, preserves
new or changed dumps locally without deleting their device originals, and rejects
a run with crash evidence even if its performance capture is complete. A failed
dump inspection is not reported as a clean run. Historical captures without this
check carry `clean_shutdown_verified: false`; known-crashed runs are rejected by
the directory comparison command.

FTP reuses one connection and probes pending results with `SIZE` instead of
opening data transfers for missing files. A local device lock excludes concurrent
runners. A process interrupted outside Python's cleanup path can be recovered:

```sh
python3 tools/vita_fps_benchmark.py recover build/fps-autobench/RUN_ID
```

Recovery only accepts the recorded benchmark or original hash and a verified
backup. It refuses an unrelated installed executable. Failed or timed-out captures
are not treated as performance results. Network/control failures and app stalls
have not all been attributed; the automation records them rather than claiming
that every attempted run completed.

A typical run and comparison are:

```sh
python3 tools/vita_fps_benchmark.py run \
  --package build/vita-fps-benchmark/platform/vita/DK64FPSBenchmark.vpk \
  --original build/releases/DK64Recompiled-34b4d0f.vpk \
  --rom donkeykong64.us.z64 \
  --seed-directory build/fps-autobench/preflight/normal-saves \
  --output build/fps-autobench/UNIQUE_RUN_ID --run-id UNIQUE_RUN_ID \
  --scenario attract --seconds 120 --profile-every 0

python3 tools/compare_vita_fps.py REFERENCE_DIRECTORY CANDIDATE_DIRECTORY \
  --workload rap-crowd --output comparison.json
```

A new run ID is required; existing remote receipts are not silently reused.
Use `world` and `--workload world-idle` for the stationary free-roam comparison.
The runner's `--original` package must match what is actually installed, not
merely the example version above.

## Correctness and rejected experiments

Host checks cover retained draw ownership, texture invalidation, batching and
framebuffer dependencies, partial CPU writes, depth sharing, photographs,
translucent primitive order and generated gameplay hooks. The renderer controls
use Mesa GLES with AddressSanitizer; host timing is not substituted for hardware
FPS.

The physical depth diagnostic completed 60 exact depth-image/partial-byte cases,
100 redraw/readback iterations and 100 draw/present/readback iterations. It also
completed five 1,000-draw submission controls. Its receipt is in
`build/fps-autobench/physical-depth-01/`; it restored the normal executable and
verified unchanged normal saves.

An exclusive-presentation experiment skipped redundant swaps of unchanged
images. A matched Rap run changed measured game FPS from 17.937 to 18.013, a
0.42% difference, despite reducing swap calls. That is not a demonstrated
throughput gain. A separate physical scanout control retained the first 20 solid
color images correctly but failed its subsequent CPU-write pixel check. The
failure was not established to be caused specifically by retention, and is not
being hidden by relaxing the check. **The entire presentation-retention change
is excluded from the delivered renderer.** Its source snapshot and packages are
kept only under `build/fps-autobench/rejected-presentation-experiment/` and the
local experiment evidence. Normal presentation behavior remains unchanged.

The earlier unsafe/inconclusive vitaGL scratch-buffer experiment remains disabled.
No new GPU-buffer lifetime shortcut, lowered resolution, reduced draw distance,
frame dropping, shader fast-math policy, thread-priority change or audio rewrite
is part of this pass.

## Local reference material

The investigation used the pinned vitaGL source in the existing dependency image
(`/tmp/vitagl`), rather than internet performance recommendations. Sony SDK
references were read locally with `rga`: `libgxm-Overview_e.pdf`, vertex-stream
state (p. 20), ring-buffer pressure (p. 21), synchronization (p. 32) and GPU memory
lifetimes (p. 51); `Kernel-Reference_e.pdf`, thread execution counters (p. 25);
and `Display-Reference_e.pdf`, framebuffer queries (p. 11). These explain API
contracts and possible costs, not measurements of this game's bottleneck.

## Remaining limits

The port still has scene-dependent performance and residual audio issues. A
small set of matched physical-device scenarios does not establish every level,
effect, save transition, thermal condition or long-duration session. The saved
camera regression and this pass's host/depth controls do not constitute a fresh
physical fairy-capture playthrough. Further performance work should use the same
separation between game-frame production, CPU rendering cost and display updates.
