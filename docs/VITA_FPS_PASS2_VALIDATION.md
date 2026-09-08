# Physical Vita performance, second pass

## Result and scope

RT64 commit `b47c055` groups consecutive known triangle commands and uses exact
NEON comparisons for CPU texture-cache candidates. The DK64 integration retains
its game timing, resolution, filtering, draw order, framebuffer ownership and
synchronous packed-depth queries. These changes improve measured physical-Vita
throughput; they do not establish 60 newly rendered game frames per second or a
complete-game compatibility certification.

Two clean Rap comparisons against the previously published RT64 `47ea123`
renderer show **6.0–6.6% more new game frames** and **18.2–18.9% lower graphics-task
latency**. The two matched world comparisons show **5.3–7.7% more game frames** and
**15.4–15.7% lower graphics-task latency**. Both sides use the corrected dependency
configuration described below, with profiling compiled out.

| Workload | Reference game FPS | Candidate game FPS | Reference mean graphics task | Candidate mean graphics task | Reference / candidate game-interval p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rap pair 1, 540 frames | 18.824 | 19.950 | 38.930 ms | 31.834 ms | 64.603 / 52.555 ms |
| Rap pair 2, 540 frames | 18.733 | 19.963 | 39.000 ms | 31.637 ms | 64.497 / 52.833 ms |
| World pair 1, 80 frames | 18.342 | 19.745 | 34.870 ms | 29.503 ms | 67.342 / 61.471 ms |
| World pair 2, 80 frames | 18.506 | 19.494 | 35.044 ms | 29.532 ms | 68.359 / 62.425 ms |

Evidence: `compare-corrected-rap-01.json`, `compare-corrected-rap-02.json` and
`compare-corrected-world-01.json` and `compare-corrected-world-02.json` under `build/fps-pass2-20260908/`. Each accepted
run has a complete capture, runtime-join receipt, no new coredumps, unchanged
normal saves and readback-verified restoration of the normal executable.

## Cumulative change from the installed renderer

The installed normal package `34b4d0f` uses RT64 `df49d8f`, predating both the
published `47ea123` preparation/ownership changes and this pass. A separate
control reconstructs that renderer with the current bounded benchmark, matching
profiler data structures and the same corrected dependency image. Production
frontend/runtime differences were checked separately from benchmark additions.
This is a renderer-version comparison with the common recorder, not timing
collected from an unmodified normal executable.

| Workload | Earlier renderer game FPS | Current game FPS | Earlier mean graphics task | Current mean graphics task | Throughput gain |
| --- | ---: | ---: | ---: | ---: | ---: |
| Crowded Rap | 16.474 | 19.950 | 46.685 ms | 31.834 ms | 21.1% |
| Fixed world idle | 17.201 | 19.745 | 40.773 ms | 29.503 ms | 14.8% |

Mean graphics-task latency falls by 31.8% and 27.6%, respectively. Both controls
completed cleanly. Evidence: `installed-base-rap-01/`, `installed-base-world-01/`,
`installed-reference-source.json` and `compare-installed-*.json` in the pass
directory. These replace reliance on the older teardown-crashed measurements;
see [the crash investigation](VITA_BENCHMARK_CRASH_VALIDATION.md).

## Retained renderer changes

The interpreter groups only known pure F3D/F3DEX/F3DEX2 triangle-handler pointers.
It prepares draw state once per group and caches transformed vertices until the
next non-triangle command. Calls, loads, state changes, unknown handlers and
extensions remain ordering boundaries. A group holds at most 6,144 vertices, and
an exception cancels unpublished geometry. Flat shading is applied separately to
each triangle after caching its original vertex color.

The differential regression expands grouped submissions and compares exact
primitive order, coordinates, flat shading, texture pixels, tile selection,
changed color-image dimensions, primitive depth, unknown handlers, capacity
boundaries and restart after an invalid vertex. F3D and F3DEX variants also compare
with grouping disabled. Retained draws continue to own their data.

The texture-cache change compares every byte of the 4 KB TMEM snapshot with NEON
XOR/OR operations. It does not trust a hash as proof of identity, omit bytes,
change decoding or bypass framebuffer provenance. Non-NEON targets retain
`memcmp`; `RT64_FAST_REFERENCE_TEXTURE_COMPARE` selects that control on ARM.
A forced hash collision must still reject a stale image.

The ARM64 ASan/UBSan check and physical ARMv7 control cover 65,536 individual byte
mutations, 16 alignment pairs and equal-bit differences across vector lanes and
chunks. At 500 MHz, five physical batches of 10,000 comparisons took 116.5–117.1 ms
with the reference and 34.9–35.4 ms with NEON. That isolated 3.3x comparison speed
is not a gameplay FPS multiplier.

## Correctness and packaging checks

The source with the unrelated audio follow-up excluded passes all **40 host
checks**, including GLES images, depth queries, generated DK64 query/photo code,
runtime scheduling and triangle/cache regressions. The runner and comparison
tools pass **36 Python tests**. The existing audio source and shared-file changes
are preserved separately and are not part of this performance publication.

Physical controls against the corrected dependency passed:

- 1,024 lit/texture-generated vertices and immutable triangle copies, with zero
  warmed triangle allocations.
- All 60 exact depth images and partial-byte queries.
- 100 redraw/query iterations and another 100 with presentation.
- Five 1,000-draw submission batches with completed-depth verification.

Both controls preserved normal saves, restored the normal executable and produced
no new dumps. Receipts are in `final-cpu-control/` and `final-depth-control/`.

The quiet package has benchmark, function profiling, stage profiling, scripted
input, renderer tracing and audio capture disabled. Its ELF has no corresponding
probe/profile symbols or debug sections; the depth bridge remains present. A
fresh rebuild of all renderer and frontend objects reproduced the quiet ELF and
executable hashes exactly. The VPK contains only `eboot.bin` and `sce_sys/param.sfo`
with title ID `DK64RE001`; it contains no ROM. See `corrected-quiet-checks.json`
and `delivery-source-quiet-verification.json` for the actual artifact checks.

## Measurement contract

Actual clocks are **500/222/222/166 MHz** for CPU/GPU/bus/GPU crossbar. The user's
existing 500 MHz CPU setting is retained; the gain is not attributed to a new
clock request. Both sides use identical copied private saves and comparable
shader-cache conditions.

Rap selection requires exactly one game and one graphics record for each timer
value 120–659 in map 76, mode 2, cutscene 7. World selection retains 80 idle records
after 30 warmups at approximately `(2790.549805, 500, 2144.711670)` in map 34. One
otherwise clean repeat, `matched-base-world-02`, started about 0.57 units away and
was rejected by the location check; its timing is not used as a matched control.
The world route advances by observed game progress, not a wall-time input script.

Game-loop iterations, graphics tasks, presentation requests and actual
`vglSwapBuffers` calls are distinct. Re-presenting a framebuffer does not create
a new game frame. Graphics-task wall time includes preemption and submission
waits; it is not isolated GPU execution time or a CPU-utilization percentage.

`RT64_FAST_PROFILE` must be compiled out for FPS acceptance; a runtime sampling
interval of zero is insufficient. Coarse profiling removes the hottest triangle,
preparation, texture-fast-hit and batch scopes, but its results remain diagnostic.
The comparison tool rejects compiled profiling, watchdog captures, external-input
attempts, mismatched clocks/saves, missing scene coverage and failed lifecycle
checks. Source audits include staged and unstaged changes; immutable package hashes
identify the executables actually run.

## Remaining measured costs

The corrected coarse Rap capture pairs 1,158 depth requests in the 540-frame
window. Summed per game record, caller time is 7.643 ms: 6.262 ms in the graphics
worker, 1.272 ms before reaching that worker and 0.109 ms waking the caller.
These diagnostic intervals do not support an assumed large wakeup penalty for
every query and must not be added into a CPU-utilization estimate.

Sampled cache-miss reads average 5.745 ms, including 4.592 ms in mandatory
`glFinish`, 0.786 ms in transfer/copy and 0.027 ms setting up the depth resolve.
Cache-hit reads average 0.247 ms overall, with about 0.004 ms in the readback scope.
Returning previous-frame depth would break the visibility/camera contract and is
not an accepted FPS fix.

Instrumented graphics tasks average 35.016 ms, including 7.721 ms interpreter
work, 5.829 ms vertex work, 5.135 ms TMEM loading, 4.689 ms upload/submission and
3.917 ms draw-state work. Of 540 game records, 530 report a three-VI lag, eight
report four and two report five. Smaller stage costs therefore need not produce
proportional FPS gains while total work still crosses the same VI intervals.
Normal modes retain the existing minimum two-VI lag and two-tick frame delta.

Evidence: `corrected-coarse-rap-01/roundtrip-analysis.json` and
`corrected-coarse-rap-01/stage-analysis.json`. No simulation-speed change,
interpolated-frame claim or duplicate-presentation relabeling is used.

## Dependency correction and build provenance

The cached `dk64-vita-perf-quiet` image unexpectedly still enabled
`USE_SCRATCH_MEMORY=1`, despite removal of that experiment from the source
Dockerfile. Its installed `libvitaGL.a` matched the scratch-enabled build and
exported both scratch-policy globals, initialized true. Omitting a runtime setup
call did not disable that compiled default. Earlier pass-2 comparisons used the
same stale dependency on both sides and cannot validate the intended release
configuration. They are kept as earlier observations, not mixed into the final
acceptance tables.

The Dockerfile now explicitly sets `USE_SCRATCH_MEMORY=0`. The corrected image
retains vitaGL `cd3791e` and vitaShaRK `df24065`, with `NO_SPLASHSCREEN=1`,
`READBACKS_SPEEDHACK=1`, shader logging off and error logging off. Its library no
longer exports the scratch-policy globals. Packed-depth reads remain synchronous.

- Image: `sha256:cac4379657612cad033a8a0b47026d394e4049a792a684995a45bab0f09719eb`
- `libvitaGL.a`: `5dca668902646a98e2af5f0d57b3ee4b074dfd52f567f592051aacd6dc30a3f1`

A separate Make-cache audit caught objects being reused after a source snapshot
was mounted at the same path. The affected control was interrupted before launch,
restored the original executable and generated no dump. Final source-layout
comparisons force both renderer and frontend recompilation. The intermediate
`corrected-baseline`, `corrected-candidate` and `corrected2-candidate` artifacts
are excluded. The validated controls are `matched-baseline` and
`corrected3-candidate`; their hashes and effective flags are recorded separately.

## Unresolved capture stalls and foreground handling

Three unprofiled attempts timed out after reaching initialization:
`texture-neon-rap-01`, `corrected-base-rap-02` and `matched-candidate-world-02`.
Each generated no new dump; the original executable and normal saves were
verified afterward. Subsequent runs of both reference and candidate completed.
The cause remains unproven; a passing retry is not presented as a fix.

Separate observer-thread and marker-only diagnostics have completed without
reproducing the stall. Four low-overhead, threadless binary-progress captures
(three world, one Rap) all reached export and clean shutdown; this does not
identify the cause of the earlier failures. The expanded watchdog records progress around the SDL pump
and recorder loop through the host deadline and joins before normal teardown.
Diagnostic packages are explicitly excluded from FPS comparisons. Run-specific
startup, watchdog and progress files are collected after restoration, including
failed attempts. Failed captures now also verify normal saves automatically, and
the uploader verifies staged bytes before replacing the executable. Directory
comparisons require shutdown, restoration and save-preservation receipts. The
extra watchdog imports require a larger ELF segment gap;
that linker option is restricted to diagnostic builds.

The earlier Age of War launch-state problem was resolved by closing its verified
`AOWV00001` process. Records do not establish which prior action opened it. The
runner now requires a fresh DK64 startup receipt and sends **no global controller
presses or resets**. All game navigation is supplied inside the DK64 fixture.

For system screenshots, wait at least ten seconds after Home+Start and require a
new image. A separate 350 ms chord produced five identical black PNGs and a new
`SceAvMediaService` system dump; DK64 itself joined its runtime and its executable
and saves were restored. Those images did not establish the visible scene, and
the run is excluded. Raw dumps remain private in `screen-check-01/`.

## Source-guided experiments not retained

Reference ports were cloned and inspected locally. Revisions and relevant
observations are recorded below; decomp/native-asset assumptions were not copied
into an original-display-list renderer without checking the different lifetimes.

| Reference | Revision | Relevant observation |
| --- | --- | --- |
| Ghostship | `bcc70fb` | Borrowed `vglBufferData` streams use an advancing mapped scratch allocation; the Vita build uses `-O3`, fast math and mathneon. |
| Starship | `e57a615` | Uses `-O3` and fast math but explicitly disables LTO. |
| Perfect Dark | `fe69821` | Borrowed streams and several project-specific compiler-pass exclusions. |
| sm64-vita | `1dcb595` | Uses `-O2`, Cortex-A9 tuning, NEON and mathneon. |
| Shipwright | `f149db4` | Retained as an additional local renderer reference. |

The installed compiler already defaults to hard-float ARMv7-A, NEON, Thumb and
Cortex-A9 tuning. Explicit ARM/Thumb and RT64-LTO trials on the earlier dependency
showed no useful throughput gain. Generated-game `-O2`, `-O3` and `-Os` trials gave
19.964, 19.974 and 19.975 Rap FPS, respectively, also on that earlier dependency.
The normal game retains `-O2`, with no new fast-math or rounding assumptions.

Borrowed GPU streams require explicit lifetime and destruction rules; the unsafe
scratch shortcut remains excluded. Unchanged-image presentation retention was
also previously rejected and is not re-enabled. Source inspection confirmed that
framebuffer RAM comparisons already use memory epochs, batch state comparisons do
not copy shared texture owners, and aligned TMEM loads already transfer eight
bytes at a time. Those were not reimplemented as purported new optimizations.

Full local evidence, immutable packages, compiler overlays, scoped-source checks
and restoration receipts are under `build/fps-pass2-20260908/`. The unrelated audio
work remains separate. The isolated Vita3K/Vulkan game capture also completed and
joined its runtime, but Vita3K later crashed during emulator teardown; that is
not a clean emulator exit or physical performance evidence.
