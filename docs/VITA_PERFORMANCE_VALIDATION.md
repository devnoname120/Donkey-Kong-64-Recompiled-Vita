# CPU rendering cost and audio under graphics load

For the subsequent unattended physical-device FPS measurements and renderer
optimizations, see [Physical Vita FPS validation](VITA_FPS_VALIDATION.md). The
measurements in this document remain the earlier controls described below.

## Scope

The hardware report for build `429aa71` described roughly 25-40 presentation FPS
in gameplay, worse performance in crowded DK Rap scenes, and frequent tiny audio
silences when rendering slowed. Those are user observations, not measurements
collected by the test harness. The changes below preserve rendering resolution,
filtering, blending order, game timing and audio rate. They do not establish a
physical-Vita frame-rate gain or guarantee 60 FPS.

## Audio task ownership

The CPU renderer must retain guest graphics inputs until decoding finishes.
Previously, this was implemented by deferring SP completion until `send_dl`
returned. However, the runtime's `osSpTaskYield` wrapper did nothing, and
`osSpTaskYielded` always returned false. DK64's original scheduler asks to yield
an active graphics task before starting audio. A slow renderer therefore delayed
audio even though audio synthesis has its own native worker.

A renderer can now opt into logical graphics yielding. Yield acknowledges SP
ownership transfer without falsely completing graphics or decoding it twice.
The renderer continues on its own worker, while the original scheduler can run
the audio task. Graphics SP completion remains deferred while suspended; on resume,
it is delivered immediately only if graphics actually finished. Final SP and DP
are both deferred until resume: DK64's deferred-VI presentation path can release
a framebuffer on DP alone. An early version deferred only SP; a native menu
transition stalled, while the matched yield-disabled control progressed. The
final implementation retains both completion events. Guest inputs are not released by
the yield acknowledgement. The task identity, state changes and completion
notifications are serialized. Renderers that do not opt in retain their prior
behavior.

`dk64_rsp_yield_checks` covers the wrappers, completion before a yield, completion
while suspended, resume before completion, repeated preemption, task-identity
mismatches and duplicate-completion rejection. The wrapper regression failed
before the implementation because the yield request never reached the runtime.

A 25-second host run executes the real recompiled game and audio code with a
capture-only renderer. An optional delay is applied to each graphics task, not
the VI timer. The audio reference device drains 1,024 stereo frames at each
22,050 Hz device period and uses the production queue-reserve helper. Padding is
counted after five seconds of warmup. This is a scheduling experiment, not a
physical-device or GPU performance measurement.

| Graphics delay | Yield support | Completed graphics tasks | Simulated padding frames after warmup |
| --- | --- | ---: | ---: |
| 40 ms | Off | 353 | 185,744 |
| 40 ms | On | 425 | 0 |
| 80 ms | On | 241 | 0 |

The final isolated runs are under `build/performance-20260907/reviewed-gfx*` with
logs alongside the directories, after the SP/DP ownership correction. An earlier overlapping-build/emulator run had
padding even with yield enabled. Yield does not create CPU capacity or guarantee
that synthesis meets its deadlines under arbitrary host contention. The isolated
repeat, rather than that confounded comparison, supplies the table.

Reproduce with separate copies of test saves and the local ROM:

```sh
build/host-probe/dk64_runtime_probe DATA_DIRECTORY 25 0 1 attract capture 40 1 0
build/host-probe/dk64_runtime_probe DATA_DIRECTORY 25 0 1 attract capture 40 1 1
```

The last three arguments select per-task graphics delay, the block-draining audio
reference device with PCM timing, and graphics-yield support. Without audio timing,
the existing host probe still uses its original unbuffered observation callback.

## Rendering submission

The pinned vitaGL `glEnable(GL_SCISSOR_TEST)` and `glScissor` implementations mark
the scissor mask dirty even when the requested state is unchanged. RT64 formerly
made both calls for every submitted draw. It now queries the actual enabled state
and scissor box and submits changes only when needed. Querying actual state keeps
internal blits and external GL users from invalidating a private cache.

The GLES regression originally observed 32 scissor box calls and 32 enables for
32 identical draws. It now observes zero of each, with the same pixels. Additional
checks change the clip box, modify GL state externally, and pass through a
presentation before drawing again.

A separate native ARM control runs 1,000 immediate submissions with the same
320x240 depth-tested geometry. Five timed batches follow shader warmup. The final
readback verifies the expected depth and includes completion costs separately
from submission time.

| Median per 1,000 draws | Before | After |
| --- | ---: | ---: |
| Submission | 6,837 us | 4,788 us |
| Submission plus completed depth query | 75,682 us | 61,010 us |

These are sequential Vita3K/Vulkan software-renderer measurements, not gameplay
FPS. All 60 depth-image cases and 200 redraw/query stress iterations passed in
both runs: `perf-submission-before` and `perf-submission-after` under
`build/vita3k-depth-validation/`.

## CPU vertex processing

Light and look-at directions are invariant during a single vertex-load command.
They are now transformed and normalized once per load, instead of repeating the
same work for every vertex. The values are not cached across commands, so later
light, matrix or look-at changes need no new invalidation rules. The existing
normal, lighting, texture-generation and viewport semantics remain unchanged.

Triangle submission also reuses a renderer-owned three-vertex allocation. Sinks
already have a consume-or-copy-before-return contract; retained draws own their
copies. A regression checks those copies and counts allocations after warmup:
1,000 triangles formerly caused 1,000 temporary allocations and now cause zero.

`rt64_fast_rsp_checks` compares 1,024 lit/texture-generated vertices across zero
through seven lights, changed transforms, zero directions and texture-generation
modes. The same test body runs in the separate `RT64CpuControl.vpk` title
`RT64C0001`, using `ux0:data/rt64-cpu/results.log`.

| Median CPU-control workload | Before | After |
| --- | ---: | ---: |
| 640,000 lit/texture-generated vertices | 588,473 us | 449,154 us |
| 200,000 triangles into a capture sink | 213,143 us | 93,464 us |

Five trials used the exact Linux AArch64 Vita3K `496939b6` executable, with CPU
optimization enabled. This runs the ARMv7 port code but does not reproduce the
Vita CPU's cache, allocator or memory bandwidth. The archives and logs are
`build/performance-20260907/cpu-before03/` and `cpu-after/`. The portable test's
normal mode requires zero warmed triangle allocations; `--benchmark` reports the
baseline count without rejecting it.

## SDK guidance and rejected experiment

The local Sony libgxm Overview's state-setting and ring-buffer guidance was
consulted when reviewing repeated state submission and allocation pressure.
Implementation decisions were checked against the pinned public vitaGL source,
in particular `source/tests.c`, `source/misc.c` and `source/buffers.c`. No Sony
SDK headers, implementation code or binaries are added to the project.

The optional vitaGL stream-buffer scratch allocator was evaluated but is not
included. Its native runs aborted in the emulator; later the previously passing
reference binary also failed with CPU optimization enabled. That prevents
attributing the failure to the allocator. The final depth control passes with
CPU optimization disabled. No circular-pool lifetime speedhack or new allocator
policy is enabled in the delivered build.

## Final integration and quiet package

The final source passed all 33 host tests, eight camera-receipt verifier tests,
the resolved upstream-patch inventory and whitespace checks. The common rendering
tests include exact color/depth pixels and CPU/GPU memory ownership, not merely
successful API returns. The full final depth control with CPU optimization off
passed 60 images, 200 redraw/query stress iterations and five 1,000-draw batches
in `build/vita3k-depth-validation/perf-final-depth-noopt/`.

The corrected yield-enabled native game completed the camera test and a new
process reloaded its resulting save. Both photographs matched every one of their
20,480 source-derived pixels; both buffers were freed, the fairy flag was set,
and normal player movement resumed. The restart verified permanent flags before
granting any prerequisites. The separate camera fixture and its existing limits
still apply; this is not proof of normal quest progression. Evidence:

```sh
python3 tools/verify_vita_camera.py \
    build/vita3k-camera-validation/perf-final-events-capture \
    --restart build/vita3k-camera-validation/perf-final-events-restart
```

The final quiet package's SHA-256 is
`397f9710b09ebeaf3baeb8892d5d5f22483b5277407d1ac27d99ea0ba3c46f81`;
its eboot SHA-256 is
`5e7395b3f5b3231f2ee886282633c8abc8ed45cffb3c62b7558efbc7f856b755`.
It retains `NO_SPLASHSCREEN=1` and the previously selected color-readback
speedhack. Diagnostics, scripted input, profiling, tracing, audio capture and
benchmark instrumentation are excluded. The rejected scratch allocator is absent.

Matched quiet-game runs used the exact Linux AArch64 Vita3K `496939b6` executable,
Vulkan/Lavapipe, external-host mapping, identical starting saves, warm shader
caches and PulseAudio recording at 48 kHz S16LE stereo. No controller input was
injected. Each emulator ran for 90 seconds; output recordings last about 88 seconds
because the recorder and emulator start at different times. Exit 124 is the
harness timeout, not clean application shutdown.

| Quiet-game capture, seconds 15-50 | CPU optimization | Gaps at least 10 ms | Total silence in those gaps |
| --- | --- | ---: | ---: |
| Baseline `429aa71` | Off | 378 | 12.709 s |
| Final candidate | Off | 1 | 0.406 s |
| Final candidate | On | 0 | 0 s |

The first two rows are the matched slower-graphics comparison. The optimized
row is a separate sanity check, not another baseline comparison. Local records
are `perf-baseline-noopt`, `perf-reviewed-noopt` and `perf-reviewed-opt` beneath
`build/vita3k-linux-control/audio-validation/`. Both final runs retain nonzero
output through recording end and unchanged save hashes.

The remaining no-optimization gap begins at 15.528 s and lasts 406.208 ms. Later
gaps of 73.167 ms and 12.583 ms occur near 63.09 s. The optimized run retains a
58.292 ms gap near 50.706 s and longer startup/loading gaps. Their different timing
and length do not establish a general loading-stall improvement. The recurring
silences under slower graphics are substantially reduced, not every possible
audio discontinuity.

No new physical-Vita FPS, CPU/GPU utilization or listening result is claimed.
Emulator CPU optimization has separate intermittent failures even with previously
passing binaries. Device testing of the staged quiet package remains necessary,
especially the crowded DK Rap scene at the same clock settings as the baseline.
