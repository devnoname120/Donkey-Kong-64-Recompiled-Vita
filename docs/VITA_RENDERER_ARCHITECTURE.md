# RT64 Vita: retained semantic rendering and native GPU packets

## Status and decision

This is a hardware-tested architecture proposal, not a completed replacement renderer. The normal game remains build `e97b563`, with RT64 `3adbf75` and N64ModernRuntime `9a6a804`. The experiments began on September 8, 2026, use the existing checkout and physical Vita, and do not change simulation timing or the normal installation.

**Recommendation:** retain RT64's compatibility interpreter, but add a semantic packet path that reuses immutable source resources, moves eligible vertex processing to GPU programs, specializes stable texture-addressing state, and submits precomputed GXM packets. Do not merely replace the vitaGL calls under the current CPU interpreter.

The order-of-magnitude objective must have a denominator. A native control measured approximately **10x less CPU submission time**. It did not make GPU completion ten times faster, and it is not a tenfold game-FPS result. A 10x reduction of the complete renderer's hot CPU work is a design target supported by several opportunities below, not an established measurement. Ten times the current game FPS is neither measured nor a meaningful acceptance criterion under the current game pacing.

Machine-readable results and experiment identities are in [the measurement record](../tools/vita_architecture/measurements.json). The [prototype sources and reproduction instructions](../tools/vita_architecture/README.md) are deliberately isolated from production builds. Local raw receipts are under `build/architecture-pass4-20260908/`; ROMs, process dumps and captured game resources are not publication artifacts.

## 1. What was measured

All accepted hardware captures use CPU/GPU/bus/crossbar clocks of **500/222/222/166 MHz** and the corrected `dk64-vita-fps-final-noscratch` dependency image. The existing clock settings were retained. Lifecycle receipts, save preservation and executable restoration are separate checks from image correctness and timing.

### Current renderer profile

A fresh coarse-profile capture of the installed renderer selected the same crowded-Rap window as the existing FPS suite: map 76, mode 2, cutscene 7, timers 120 through 659. Seventeen sampled tasks among 540 selected tasks averaged 31.766 ms. Exclusive stage averages were:

| Stage | Diagnostic time per sampled task |
| --- | ---: |
| Command/interpreter and remaining preparation | 7.727 ms |
| Vertex processing | 5.086 ms |
| TMEM loading | 2.671 ms |
| Texture lookup, hashing and exact comparisons | 7.211 ms |
| Draw-state preparation | 3.869 ms |
| Vertex upload and draw submission | 4.715 ms |
| Microcode identification and full sync | 0.250 ms |

These are instrumented wall times, including scheduling and waits encountered in calls. They are not isolated CPU-utilization measurements. The prior unprofiled measurements were approximately 28.3 ms per crowded-Rap graphics task and 25.5 ms per fixed-world task, with approximately 20 new game frames per second. Do not divide the profiled and unprofiled measurements and attribute that difference to an optimization.

The sampled Rap tasks repeatedly hit already decoded textures: approximately 209 cache hits and zero misses per sampled task. Cache-hit processing can therefore be expensive even when texture decoding itself is not occurring. Making another copying helper faster will not remove the repeated semantic reconstruction and validation.

### Reuse of real game inputs

A separate observer compared every byte of each previously seen `(address, length)` source range. It did not assume a matching hash implies equality. Command pages were observed once per task; vertex and texture ranges were observed at their actual loads.

| Observation window | Raw vertex bytes unchanged | Texture-source bytes unchanged | Command-page bytes unchanged |
| --- | ---: | ---: | ---: |
| Crowded Rap, 122 tasks | 99.06% | 99.59% | 98.19% |
| World, 75 tasks | 98.60% | 99.13% | 92.84% |

The counters include compulsory first observations. They distinguish changed contents, new ranges and exact repeated contents; retention limits were not reached. The Rap observer retained 536 vertex ranges, 94 texture ranges and 207 command pages. The world observer retained 214, 106 and 245 respectively.

This supports **retaining source geometry, texture resources and decoded command templates**, not retaining the entire previous rendered frame. Matrix commands remain frequent: approximately 427 per observed Rap task and 125 per observed world task. World input also includes vertex modifications. Identical source bytes do not mean identical incoming RSP state, transforms, texture placement or final draws. The observer does not measure the hit rate of a future write-generation cache: a game can write the same bytes again, and that still needs correct dirty tracking.

### Frozen real-scene replay

The recorder retained one actual post-batching draw stream, including primitive order and immutable texture references. The selected Rap frame had 170 packets and 11,259 expanded vertex records; the world frame had 158 packets and 7,227 records. Both were 320 x 240. The recorder rejects framebuffer-feedback, readback and CPU-write dependencies it cannot replay correctly.

The same frozen workload was then executed in rotating mode order, 24 timed trials per mode. CPU submission and serialized completion through `glFinish` were measured separately. Capture, shader warmup, source observation and image checks were outside the timed replay sections.

First independent run of each scene:

| Mode | Rap submission / completed | World submission / completed | Equivalent image? |
| --- | ---: | ---: | --- |
| Existing draw path, streaming transformed vertices | 5.598 / 13.547 ms | 5.111 / 14.726 ms | Yes |
| Same packets, resident transformed vertices | 2.990 / 12.282 ms | 3.086 / 14.034 ms | Yes |
| Resident vertices, quarter-area viewport | 3.214 / 8.009 ms | 3.189 / 9.596 ms | No; diagnostic ablation |
| Resident vertices, untextured fragment program | 2.265 / 5.087 ms | 2.265 / 4.967 ms | No; diagnostic ablation |
| Resident vertices, constant sampler descriptors | 2.821 / 7.798 ms | 2.924 / 8.317 ms | Yes in these captures |

The independent repeat completed the streamed and specialized workloads in **13.723 / 7.843 ms for Rap** and **14.664 / 8.249 ms for world**. The machine-readable record includes distributions, selected-frame identities and source receipts. The reduced viewport and untextured modes are **not proposed quality reductions**. They distinguish pixel-dependent rendering cost from the remaining submission cost. In particular, eliminating CPU interpretation does not eliminate a substantial GPU-side bottleneck.

The last mode changes stable sampler parameters from uniforms to compile-time constants: image dimensions, shifts, tile origin, clamp, mask and mirror descriptors. Texture pixels, sampling/filtering rules, color combining, blending, depth state and primitive order are retained. Constant folding removes repeated address calculations and inactive branches. The frozen Rap frame needed 11 specialized programs; the world frame needed 37.

The equivalent replay modes were compared byte-for-byte against the captured RGBA image at the checked beginning/end trials and after restoring the ordinary path. Each image contains 76,800 pixels, with 17,435 and 27,722 distinct RGBA colors in the first Rap/world captures. These are not black-image false positives. This establishes feasibility for those captured states, not general shader equivalence across all game materials. Compilation and shader-cache growth are not free and are not included in the warmed replay timing.

Crucially, the replay already contains transformed vertices and resolved draw state. It bypasses the work that a complete new frontend must still perform. **13.547 to 7.798 ms is a frozen-backend result, not a measured live-game FPS improvement.**

### Native GXM packet control

A separate native control compares streaming GL, resident GL, direct GXM calls and precomputed GXM state/draw packets. It changes matrix and tint values between two phases and checks the complete image after every timed trial. Each mode executes 170 draws and 11,220 vertex references. There are 66 unique resident vertices, one small texture and one shader pair; depth and blending are disabled. This is deliberately a submission experiment, not a model of the game's complete GPU workload.

Medians from the second independent run:

| Path | CPU submission, including measured preparation | Completed workload |
| --- | ---: | ---: |
| Streaming GL | 2.389 ms | 4.194 ms |
| Resident GL | 0.753 ms | 2.475 ms |
| Direct GXM | 0.397 ms | 2.104 ms |
| Precomputed GXM | 0.237 ms | 2.073 ms |

The first run measured 2.399 to 0.240 ms for streaming versus precomputed submission. The two submission ratios are **10.01x and 10.08x**; the completed-workload ratios are only **2.03x and 2.02x**.

A third control rebuilt from the published prototype source directory measured **9.74x** less submission time and **2.01x** shorter completed-workload time. The full observed submission range is therefore **9.74-10.08x**, not a guaranteed minimum of 10x. All three controls passed their 56 timed image comparisons and device-restoration checks.

The precomputed path's measured submission includes copying 13,600 bytes of per-draw uniform payload; its preparation subinterval was approximately 0.084 ms. The payload was prepared from two known matrix/tint phases. Fresh game animation, matrix evaluation, material classification and resource invalidation are not part of that test. Image comparisons prove that the uniform phases actually change output and that native modes match the GL controls in this fixture.

## 2. Architectural alternatives

| Approach | Benefit | Limitation / decision |
| --- | --- | --- |
| Parallelize the existing CPU renderer | Can distribute independent conversion work | Does not remove repeated work or simplify fragment programs; state dependencies and exact depth reads limit parallelism. Not the primary redesign. |
| Replace only vitaGL with direct GXM | Reduces API, allocation and state-translation overhead | Leaves command interpretation, CPU vertex processing and TMEM/cache work intact. Useful backend component, insufficient overall architecture. |
| Retained semantic frontend plus GPU vertex modules, specialized materials and precomputed packets | Attacks repeated work and GPU shader overhead together | Larger correctness surface and memory/cache management burden. Recommended, with staged fallback and explicit gates. |

A captured-frame replay is a diagnostic lower-bound experiment, not a fourth production architecture. Reusing a whole frame would freeze animation and lose game-side memory changes.

## 3. Proposed data flow

```text
Original guest display lists + RDRAM writes
                |
       versioned resource registry
                |
 guarded decoded command templates
                |
   semantic mesh/material instances
       /                         \
CPU compatibility fallback    GPU RSP modules
       \                         /
        ordered packet submission
        precomputed GXM backend
                |
     versioned color/depth resources
          /                  \
 exact guest readbacks     VI presentation
```

The public boundary should describe N64 rendering semantics and resource lifetimes, not expose GXM pointers. Keep GXM packet implementations in the Vita backend and retain a host implementation for differential validation. DK64-specific fixtures and any future timing/interpolation integration remain outside generic RT64.

### A. Versioned resource registry

Introduce typed handles for source ranges, texture interpretations, mesh blocks and framebuffer generations. A handle contains identity plus a monotonically changing content generation; a host address by itself is not identity.

Extend the existing write-observation mechanism conservatively. Writes mark watched source pages/ranges dirty. At a synchronization boundary, compare or rebuild only dirty dependencies. Pages written with identical contents can reuse exact validated templates after one comparison. Instrument the overhead imposed on the game thread; dirty tracking that moves the same cost into every guest store is not a win.

Texture keys must include relevant format/size, source generation, stride, dimensions, palette/TLUT generation, tile interpretation and framebuffer provenance. Preserve partial loads, odd-row TMEM permutation, bank splitting, wraps and overlapping palettes. Simple complete ordinary-RAM loads can resolve directly to canonical resident textures without rebuilding and hashing all 4 KiB of TMEM for every hit. Mixed/partial/GPU-backed cases initially use the compatibility materializer.

Generations protect cache reuse, but **do not make in-flight memory immutable**. A load must bind an immutable snapshot or retain a proven input lease until its data has been consumed. Materialize/copy uncertain mutable inputs before releasing that lease. A later generation check cannot recover bytes that were already overwritten. Keep the current SP/DP yield and input-release ordering initially; do not equate a GXM notification with a guest scheduler event without separate proof.

### B. Guarded command templates, not unconditional display-list replay

Decode stable command pages into typed operations once. This is an internal intermediate representation, not writable native-code generation. Each entry is guarded by microcode identity, source generations, entry-state requirements and the original bounded command/control-flow rules.

Template evaluation must still apply dynamic matrix/viewport/light/tile state. Do not cache a final draw solely because its command bytes are unchanged. Static geometry runs become mesh-instance templates; changing matrices or material bindings patch parameter blocks rather than reconstructing every triangle.

Calls, returns, branches, sync operations, vertex modifications and framebuffer dependencies remain explicit boundaries. Unsupported templates fall back at a defined state boundary without replaying side effects or submitting geometry twice. Preserve the existing instruction budget and malformed-input checks. Begin with known F3DEX2 shapes, not an attempt to recognize every N64 microcode or game-specific display-list idiom.

### C. Resident source meshes and GPU RSP modules

Store canonical vertex input once, with indexed geometry and explicit per-load transformation bindings. Avoid expanding every triangle into another 44-byte CPU-transformed vertex record each frame. A mesh identity includes the source generation and decode layout; an instance carries matrix/light/look-at/viewport/fog/texture-scale state.

GPU modules implement eligible RSP transformation, lighting, texture generation and fog. The measured native control demonstrates GPU matrix updates, not complete RSP equivalence. That larger module still needs implementation and differential tests.

Specific correctness requirements:

- Preserve the transform order and the fact that viewport state is captured when vertices are loaded, not when a later triangle is drawn.
- Transform and normalize light/look-at directions in the same space as the current implementation, including nonuniform scale.
- Signed normals must preserve `int8 / 127`, including -128; blindly using a normalized signed-byte attribute can change that value.
- Preserve texture-generation, fog/alpha and primitive-depth behavior; do not enable global fast math or reassociate operations without validation.
- Maintain loaded-vertex identities across partial replacement and vertex modifications. A later write must not mutate a previously submitted instance.
- CPU branch-Z/W queries require an exact compatible shadow calculation on demand, not a GPU readback for every vertex. Frequent shadow fallback can destroy the expected speedup and must be measured.
- Flat shading needs the original provoking vertex semantics. Initially retain CPU fallback for unsupported flat-shaded or screen-modified groups rather than relying on an incompatible GL provoking-vertex convention.

Fallback is a first-class module, not a failure mode hidden from the profiler. Count the vertices, draws and time that use it. The hot CPU target requires high GPU-module coverage; 99% unchanged source bytes alone does not guarantee that coverage.

### D. Material and sampler specialization

Replace repeated shader-side interpretation of stable sampler state with bounded specialization. A typed material key separates structural sampling choices from volatile parameters. Keep scrolling origins or frequently changing parameters as uniforms when specializing them would cause unbounded shader churn.

Use a generic equivalent shader as the immediate fallback. Promote frequently used stable descriptors into specialized variants; retain source/key identity and cap program count and memory. Compile/prewarm during safe loading opportunities where possible, and record cold compilation latency separately from warmed frame time. Do not invoke one shared GL/GXM context concurrently from compiler and render workers.

The proof substitutes all descriptor values in frozen captures. Production should test a smaller structural key first: wrap/mirror/clamp modes, filter path, texture format and common dimensions. Measure whether most of the 4-6 ms completed-workload saving survives that less aggressive specialization. Exact negative-coordinate floor wrapping and three-point filtering must be preserved.

### E. Precomputed GXM backend

Resolve immutable mesh/material state into precomputed vertex state, fragment state and draw packets. The hot path patches dynamic parameter blocks and binds prepared resources instead of rebuilding GL attribute descriptions, allocating a VBO and rediscovering the same program/state on every draw.

Use explicit frame/epoch ownership for mutable uniform and geometry storage. Reuse memory only after the corresponding GPU consumer is complete. Vertex/VDM and fragment resources have distinct retirement points; textures cannot be recycled merely because vertex processing finished. Program objects and referenced precomputed structures must also remain alive.

Keep a single owner for the immediate context. Deferred contexts are an optional coarse-grained preparation mechanism, not permission to call the immediate context from multiple threads. A deferred command list is not automatically an immutable reusable display list: its execution tail is patched. Use per-in-flight storage or a documented completion boundary before reuse.

Do not mix native state with vitaGL's private state cache casually. During migration, either keep well-defined exclusive backend ownership or invalidate and restore all affected state at each handoff. The prototype explicitly unsets precomputed vertex/fragment state before returning to GL; a production hybrid needs a complete contract, including targets, masks, uniforms, textures and vertex streams.

Start with bounded caches and backpressure, not a grow-forever scratch allocator. A provisional planning budget is at most 12 MiB of additional CPU-side templates/metadata and three 1 MiB mutable epochs, while reusing rather than duplicating the existing texture-residency budget. These are proposed caps, not a measured assertion that all game levels have that spare memory. Allocation failure must evict eligible entries or fall back safely; never retire live GPU resources.

### F. Exact readbacks and presentation as explicit dependencies

Color/depth images carry content generations and completion dependencies. CPU queries request the correct generation, never the previous frame's depth. Preserve packed-depth quantization, odd-width surfaces and partial-byte queries.

The previous diagnostic measured approximately 7.643 ms of depth caller time per game record, of which only 0.109 ms was caller wakeup. Queueing/wakeup is not the dominant demonstrated cost. That historical measurement should not be added to other overlapping intervals as though it were independent CPU work.

Initially keep the correct synchronous path. Then test generation-aware early resolve: submit a depth resolve while rendering's inputs are still known, attach completion information and consume it only when a real query requests that generation. This may overlap work, but can waste bandwidth if queries do not occur. Batch requests only where the game/runtime can prove independence without reordering decisions. A tiny query atlas or selective resolve is an experiment requiring exact format, synchronization and cost validation, not permission to drop `glFinish` from the existing path.

## 4. Performance model and uncertainty

The following are **engineering budgets for a warmed eligible workload**, not measured timings of an implemented renderer:

| New hot CPU work | Target / estimated range per comparable task | Basis and missing proof |
| --- | ---: | --- |
| Dirty dependency validation and resource lookup | 0.2-0.6 ms | High observed reuse; write-hook overhead and real generation hit rate unmeasured. |
| Template evaluation and instance parameter preparation | 0.6-1.2 ms | Avoid repeated triangle/vertex interpretation; no complete template frontend measured yet. |
| Native packet patching and submission | 0.4-0.9 ms | 0.237-0.240 ms in the simpler native fixture; allowance for real pipelines/textures and larger parameters. |
| Transitions, retirement and small compatibility fallback | 0.2-0.7 ms | Requires bounded caches and high eligible-path coverage. |
| **Total** | **1.4-3.4 ms** | A target envelope, not a statistical confidence interval. |

That is the route toward roughly an order-of-magnitude reduction in the current renderer's CPU-side preparation/submission cost. It requires all components to work together. The current graphics-task latency is not pure CPU execution, so an exact full-renderer CPU speedup must be measured after waits are separated under a common harness.

GPU and end-to-end limits remain material. Frozen real-scene replay with resident vertices and specialized samplers completed in approximately 7.8-8.3 ms in the first runs. A complete dynamic implementation adds GPU RSP work, per-frame resource updates, presentation and any non-overlapped query work. A provisional target is **8-14 ms for an eligible renderer workload completed on the GPU**, with a stress margin of 12-20 ms for a complete render service including transitions/queries. These ranges are design allowances, not extrapolated game-FPS measurements, and their components can overlap.

A pure native-submission rewrite cannot turn a measured 12-14 ms frozen backend into a 1 ms renderer. The native fixture already demonstrates the distinction: CPU submission improves 10x while completion improves only about 2x. Similarly, the real-scene shader specialization and residency gains must not be multiplied by the native fixture's 10x ratio.

### What 60 FPS would require

`platform/vita/timing.cpp` retains a minimum two-VI lag in normal modes and normally advances simulation by two ticks. Current selected scenes mostly span three VI ticks. A faster renderer can help cross from approximately 20 to 30 new game updates per second, but it does not by itself establish 60 normal game updates.

There are two separate possible objectives:

1. Preserve current simulation semantics and aim for stable 30 game updates with 60 Hz rendering through explicit transform/camera interpolation. That creates new rendered views, not new simulation states. It needs stable object/mesh identity, discontinuity handling and real input-latency tests.
2. Rework the game's simulation/timing behavior for 60 updates. That is a separate game-specific compatibility project involving animation, movement, timers, scripted scenes and audio scheduling, not a renderer flag.

Neither option is implemented or promised by this design. First reduce the critical-path work and validate stable lower-lag behavior without changing simulation speed. The user's 60-FPS objective is motivation for the architecture, not a reason to mislabel duplicate presentations or assume a particular final bottleneck.

## 5. Migration and acceptance gates

**Milestone 1: bounded material specialization and resource identity.** Preserve the existing renderer and add typed keys, bounded specialization, invalidation tests and cold/warm compilation measurements. Compare matched unprofiled real-game captures. Require exact sampler regressions, photograph/depth checks, cache eviction tests and no worsening of long-frame behavior. The frozen shader result is the strongest immediately actionable GPU lead.

**Milestone 2: semantic mesh/template path.** Implement source generations and a guarded F3DEX2 template subset. Keep a switchable compatibility reference. Measure game-thread write-tracking overhead, template hit rate, CPU fallback time and memory high-water marks. Test same-address replacement, writes that restore identical bytes, partial TMEM/TLUT updates, microcode changes, call/return boundaries and forced hash collisions. Templates must not skip incoming-state effects.

**Milestone 3: GPU RSP modules and native packets.** Extend vertex tests through matrices, nonuniform lights, texture generation, fog, negative/zero W, viewport changes, partial cache replacement, flat shading and screen modifications. Compare primitive order and images against the compatibility path, not just output from a synthetic shader. Require explicit epoch retirement, delayed GPU completion stress, wraparound/eviction, allocation-failure fallback and orderly shutdown. Only then measure the whole hot-path CPU target.

**Milestone 4: dependency overlap and pacing evaluation.** Investigate depth-generation resolve timing and frame scheduling using paired intervals, not summed overlapping timers. Retain guest-visible ownership rules. Report new game frames, graphics tasks, presented images, GPU completion and input latency separately. Evaluate interpolation only after basic throughput/correctness is stable.

For each milestone use the existing unattended runner with matched save seeds, clock settings, scene windows and equivalent recorder layout. Keep diagnostic/profile builds separate from FPS acceptance builds. Require complete capture, runtime shutdown, no new dump, unchanged normal saves and verified restoration. Include crowded Rap, fixed-world idle, moving-camera/transparency stress and representative framebuffer/camera effects. Historical no-export stalls remain unresolved; another successful run is not a fix.

Do not attempt a monolithic replacement before those gates. The existing CPU renderer remains the reference and a usable fallback throughout migration.

## 6. Evidence and implementation references

- Baseline production behavior and previous pass: [pass 3 report](VITA_FPS_PASS3_VALIDATION.md), RT64 `3adbf7534bc5f6b44b70105cb39a174afc581561`.
- Current profile: `current-coarse-rap-01/stage-analysis.json` in the local architecture evidence directory.
- Actual-frame replay: `specialized-rap-*` and `specialized-world-*`; run-specific architecture JSON contains mode timings, exact-image flags, dependency exclusions and reuse counts.
- Native submission: `native-packets-01/results.log` and `native-packets-02/results.log`; all 56 timed image comparisons per run passed. The public native source is original experiment code, not Sony sample code.
- Reproduction check: `published-native-01/` was built using the public prototype builder and passed on hardware. The second specialized-world capture required host-side result retrieval and guarded executable recovery after its command session disappeared; the guest had already exported and shut down. Its lifecycle, saves and restoration were independently verified. This is not evidence that the historical no-export stalls are fixed.
- Local vitaGL `cd3791e`: `source/buffers.c` (`glBufferData`, borrowed `vglBufferData`), `source/custom_shaders.c` (attribute/program preparation and draw submission), `source/gxm.c` (flush/finish and context ownership). No borrowed scratch shortcut is enabled.
- Sony SDK 3.550 English **libgxm Overview**, pp. 17, 19-24, 31-35, 42-46 and 55: context ownership, ring-buffer splitting, default-uniform lifetime, vertex/fragment notifications, precomputation, command-list memory and attribute patching. SDK **libgxm Reference**, p. 306: precomputed-state persistence and unsetting; p. 408: default uniform buffer size is in bytes. Consult the user's local SDK copy; proprietary material is not copied into this repository.

The delivered result of this investigation is the design, reproducible prototypes, measured feasibility and explicit limits. It is not a quiet production implementation of the new architecture or a claim of 60-FPS gameplay.
