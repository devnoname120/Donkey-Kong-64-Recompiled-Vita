# Physical Vita performance, third pass

## Result and scope

This pass reduces CPU work in TMEM transfers and vertex-input decoding. Against
RT64 `b47c055`, matched physical-Vita comparisons show about **10% lower graphics-task
latency in the crowded Rap** and **14% lower latency in the fixed world scene**.
The Rap still produces approximately 20 new game frames per second; the world
case improves by **1.7-2.0%**. The isolated transfer speedup is not a gameplay-FPS
multiplier, and this pass does not establish 60 FPS or resolve every capture stall.

The comparison controls retain the current recorder layout and differ only in
`RT64_FAST_REFERENCE_TMEM_LOAD` and `RT64_FAST_REFERENCE_VERTEX_INPUT`. Both flags
select the prior implementation when enabled and default to off. All final timing
builds compile stage profiling and the watchdog out. Source fingerprints, effective
flags, executable hashes and the dependency image are checked independently of VPK
ZIP hashes.

## Matched hardware results

| Workload | Reference game FPS | Candidate game FPS | Reference mean graphics task | Candidate mean graphics task |
| --- | ---: | ---: | ---: | ---: |
| Rap pair 1, 540 frames | 19.938 | 19.963 | 31.563 ms | 28.301 ms |
| Rap pair 2, 540 frames | 19.948 | 19.999 | 31.536 ms | 28.289 ms |
| World pair 1, 80 frames | 19.439 | 19.835 | 29.485 ms | 25.460 ms |
| World pair 2, 80 frames | 19.486 | 19.810 | 29.687 ms | 25.613 ms |

All eight captures have complete data, ordered-shutdown receipts, no new dumps,
unchanged normal saves and verified restoration of the normal executable. Actual
CPU/GPU/bus/crossbar clocks are 500/222/222/166 MHz on both sides; no new overclock
was introduced. Private seed saves and selected workloads match. The second pair
of each workload runs the candidate before the reference, reversing the first
pair's order.

Evidence under `build/fps-pass3-20260908/`:

- `compare-bulk-input-rap-01.json` and `compare-bulk-input-rap-02.json`.
- `compare-bulk-input-world-01.json` and `compare-bulk-input-world-02.json`.
- `final-comparison-audit.json`, the corresponding `bulk-input-*` run directories,
  and `packages/bulk-input-matched-{reference,candidate}.json`.

Rap selection requires exactly one game and graphics record for each scene timer
120 through 659 in map 76, mode 2, cutscene 7. World selection retains 80 idle
observations after 30 warmups at the same map-34 position, with the existing
0.001-unit matching tolerance. The fixture follows observed game progress rather
than moving for a fixed wall-clock duration.

A separate TMEM-only Rap comparison reduced mean graphics-task time from 31.494
to 29.211 ms while both sides remained at 19.963 game FPS. Its evidence is
`compare-tmem-rap-01.json`. This control is why transfer time alone is not presented
as an end-to-end frame-rate improvement.

## Why target these paths

The added transfer-category counters identify the actual TMEM workload rather
than assuming every transfer has the same format. In the selected crowded Rap
profile, 17 sampled graphics tasks average 384,159 source bytes, of which 383,647
are ordinary aligned block loads. Palette loads account for the remaining 512
bytes on average. The selected samples have no RGBA32, framebuffer-provenance or
unaligned source transfers. The categories are partly overlapping and are not
summed as disjoint percentages.

TMEM loading averages 5.042 ms in that profile, and vertex processing 5.808 ms.
The instrumented graphics tasks average 34.866 ms; these diagnostic wall times
include preemption and waits and are not substituted for unprofiled timings or
CPU-utilization measurements. See `profile-rap-01/transfer-breakdown.json`.

The retained changes are:

1. **Bulk TMEM loading.** After the existing complete-source bounds check and
   framebuffer-snapshot ordering check, plain aligned non-RGBA32 loads are copied
   in groups. NEON byte permutations retain word-swapped RDRAM semantics and the
   odd-row XOR. Each group ends at a TMEM wrap or DXT transition. Short tails and
   non-NEON hosts have scalar handling. Palette, RGBA32, unaligned and
   framebuffer-provenance cases keep the original path. The generation advances
   exactly once, and texture-cache collision checks remain intact.
2. **Vertex record decoding.** The already validated vertex-load span is decoded
   as complete 16-byte records on little-endian hosts instead of repeating checked
   byte accesses for every field. Signed coordinates, texture coordinates, colors
   and normals retain their exact input values. Matrix, lighting, fog, division and
   texture-generation arithmetic are unchanged. Other byte orders retain checked
   field access.

These are reusable RT64 changes, not DK64 object-count reductions or game-specific
visibility approximations. They do not change resolution, filtering, primitive
order, game timing, audio rate, framebuffer ownership or GPU-resource lifetimes.

## Native controls and correctness

The independent byte-reference test covers **2,804 exact TMEM cases**: aligned and
unaligned sources, row strides, wrap boundaries, short vector tails, DXT values,
palettes, RGBA32 bank splitting, retained framebuffer provenance, subsequent
ordinary-memory overwrite and invalid-source rejection. It verifies generation
advancement and snapshot ordering as well as all 4 KB of resulting TMEM.

On the Vita, 20,000 block transfers of 40.96 MB total took 430.927 ms with the
prior loader and 127.453 ms with the bulk loader. The tile control took 276.105 ms
and 57.407 ms respectively. These isolated controls establish less transfer work,
not a proportional game-frame gain. See `cpu-reference-01/results.log` and
`cpu-bulk-01/results.log`.

The vertex-input control covers **65,536 exact records**, both permitted record
alignments, cached viewport coordinates and preservation of the vertex cache after
invalid requests. Existing lit/texture-generated vertex and immutable-triangle
controls also pass on ARMv7. Five native batches of 640,000 lit vertices took
735.840-737.572 ms with the reference input path and 635.177-636.693 ms with the
new path. These numbers include the complete tested vertex workload, not just its
input loads. See `vertex-input-cpu-{reference,candidate}-01/`.

The scoped source, excluding the unrelated audio follow-up, passes **42 host
checks** with AddressSanitizer and the shared Mesa GLES path. The benchmark and
comparison tools pass **36 Python tests**. Separate RT64 sanitizer and profile
selection controls check the portable path and that reference switches actually
select their intended implementations.

The final physical depth control, built from the retained renderer source and the
corrected dependency, passes all **60 exact images/partial-byte queries**, **100
redraw/query iterations**, **100 draw/present/query iterations**, and five
1,000-draw submission batches. It restores the normal executable, preserves normal
saves and produces no new dump. Its receipt is `bulk-input-depth-01/validation.json`.
This is not a new physical fairy-capture playthrough or complete-game certification.

## Pacing and remaining limits

The candidate still reports three-VI lag for 537 and 538 of the 540 selected Rap
records. Each candidate world window reports three-VI lag for 79 of 80 records.
These observations are consistent with the existing VI-quantized frame deadlines:
lowering one rendering cost need not cross the next end-to-end frame boundary.
They do not prove that all remaining latency is deliberate waiting.

Game-frame production, graphics-task completion, presentation requests and actual
swaps remain separate metrics. Duplicate presentation is not a new game frame.
Normal modes retain their existing minimum two-VI lag and two-tick frame delta;
this pass neither changes simulation speed nor implements frame interpolation.
Further FPS work should measure the complete critical path, including game work,
current-frame depth queries and scheduling, instead of extrapolating from another
isolated helper benchmark. The required synchronous depth contract is unchanged.

The three historical no-export attempts described in the
[second-pass report](VITA_FPS_PASS2_VALIDATION.md) remain unexplained. No speculative
SDL, thread-info, affinity or runtime change is claimed as their fix. In this pass,
`base-world-02` lost host supervision, but inspection found its complete capture
and ordered-shutdown receipt on the Vita; guarded recovery restored the executable.
That interruption is not classified as another application stall. The earlier
[GXM teardown race](VITA_BENCHMARK_CRASH_VALIDATION.md) remains fixed by ordered
runtime shutdown, not by suppressing dump collection.

## Build and delivery provenance

All final controls use `dk64-vita-fps-final-noscratch`, image
`sha256:cac4379657612cad033a8a0b47026d394e4049a792a684995a45bab0f09719eb`.
The pinned vitaGL/vitaShaRK pair, viewport patch, disabled splash screen,
`READBACKS_SPEEDHACK=1`, explicit `USE_SCRATCH_MEMORY=0` and disabled dependency
logging are retained. Packed-depth queries remain synchronous. The rejected
scratch-buffer and unchanged-image presentation experiments stay excluded.

Renderer and frontend objects are rebuilt when producing the matched artifacts.
The four shared files containing unrelated audio edits are taken from the
published source view; the audio-only deltas and untracked files remain preserved.
No source snapshot is restored over the live checkout.

The quiet-package audit checks effective flags, ELF symbols and debug sections,
not just CMake defaults. Benchmark navigation, watchdogs, profiling, tracing and
audio instrumentation are excluded; the depth bridge and normal controls remain.
The VPK contains only the executable and SFO, not the ROM. The final release's
hashes and source revisions are recorded in its `build/releases/` validation JSON;
`build/fps-pass3-20260908/delivery/delivery.json` records actual installation and
readback verification separately from package staging.
