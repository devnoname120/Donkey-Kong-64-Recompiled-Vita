# PS Vita port development

This work is in progress. A DK64 Vita VPK builds and runs the intro, main menu,
opening Adventure story, and player movement/jumping in DK's house through
vitaGL on Vita3K/Vulkan. A native save reload also reaches movement and swimming
in Training Grounds. Broader gameplay, framebuffer effects, and performance
still require validation; this is not a completed release.

The target is a reusable reduced RT64 renderer, followed by the DK64 runtime and
Vita frontend. RT64's existing renderer uses compute shaders for vertex processing,
texture decoding, and framebuffer work. A vitaGL implementation of its graphics API
abstraction alone cannot provide those facilities.

The `RT64_FAST` build therefore retains RT64's microcode identification and GBI
command decoders and substitutes CPU RSP/TMEM processing and a small draw interface.
The Vita implementation uses GLSL ES 1.00 combiner shaders and vitaGL. Unsupported
operations must produce a diagnostic rather than silently claiming compatibility.

Reference: [Ghostship Vita](https://github.com/Rinnegatamante/Ghostship), in
particular `libultraship/src/fast/backends/gfx_opengl.cpp`: CPU display-list work,
combiner-specific shaders, GL draw state, and Vita shader compilation. Its desktop
resource system and converted display lists are not the runtime interface for this
port: recompiled DK64 produces original N64 commands in word-swapped RDRAM.

## Verified so far

- The shared GBI decoder path builds on the host and ARMv7 VitaSDK without DXC,
  Vulkan, Metal, Plume, or the desktop frontend.
- Host tests under AddressSanitizer and UndefinedBehaviorSanitizer cover command
  execution, triangles, fill rectangles, word-swapped RDRAM, TMEM tile/block loads,
  RGBA16/32, CI4 palettes, intensity textures, cache invalidation and invalid input.
- RT64 identifies the actual US ROM's graphics microcode as F3DEX2 using its
  existing hash database.
- The renderer diagnostic visibly draws an RGB triangle and background through
  vitaGL in Vita3K's **Vulkan** renderer. This is graphics-stack evidence, not a
  measurement of DK64 performance on physical hardware.
- The N64 runtime and ROM-generated game/audio code compile and link for ARMv7
  hard-float. Portable multiply tests compare 100 edge pairs and 100,000 random
  pairs against native 128-bit multiplication. Cached/uncached address-alias tests
  and save/backup-copy tests also pass.
- A deterministic queue regression reproduces the graphics-task starvation caused
  by a continuously nonempty VI producer. A fair consumer with quota 2 delivers
  waiting tasks, including a second task after the same consumer has fallen back
  to the VI-only producer. Quota 1 fails that reused-consumer case in this version
  of ConcurrentQueue. Vita's renderer lets the runtime VI thread own frame pacing.
- Worker stacks are explicitly 2 MiB. The SDK pthread archive otherwise defaults
  them to 32 KiB. The private `OSThread` layout is checked on the host and ARMv7.
- CPU tests also cover 480-line framebuffer inference, normalized viewport depth,
  light directions under nonuniform scale, linear texgen, fog/alpha behavior,
  and branch-Z fixed-point comparisons against RT64's existing shader semantics.
- A native Linux probe of the same translated game and runtime completed 4,492
  graphics tasks in 45 seconds under AddressSanitizer with a capture-only sink.
  A separately paced run completed 341 tasks in 30 seconds with the fair queue.
- The GL sink also builds against host GLES2 for comparison. Mesa softpipe renders
  the Nintendo logo and floor textures cleanly. Carrying over the desktop port's
  240p boot mode corrects the initial image size and position.
- The Vita diagnostic renders repeated and mirrored directional RGBA textures in
  Vita3K/Vulkan. Optional upload validation compares vitaGL's actual linear texture
  allocations with the decoded RGBA pixels. The game's first eight decoded texture
  hashes and first 14 draw-state/vertex traces match the host run.
- Negative texture coordinates exposed vitaGL's translation of GLSL `mod` to Cg
  `fmod`, which leaves a negative remainder. Generated shaders now use explicit
  floor modulo. Repeat, mirror, and three-point filtering of negative coordinates
  visibly pass in the Vita3K diagnostic after this change.
- The white character and floor output was isolated to a compiled RGB-only
  texture-filtering variant in Vita3K/Vulkan. Keeping texture alpha live with a
  zero-valued uniform fixes both single-cycle floor shading and two-cycle character
  shading. Coverage alpha now uses its normal assignment again; the earlier
  output-mask workaround was removed. A comparison of 86 intro draw/vertex/texture
  trace lines was identical between native host and Vita.
- Consecutive compatible draws are batched, with a 6,144-vertex batch limit.
  Tests cover ordering, state changes, depth clears, presentation, and texture
  ownership. A host GLES test also gets exactly matching pixels with batching
  enabled and disabled, including overlapping translucent primitives.
- The native frontend opens stereo audio at the game's 22,050 Hz rate and sends
  non-silent PCM to SDL (first observed buffer: 1,472 samples, peak 6,775). This
  establishes generated/submitted audio, not a full audible quality check.
- The fast draw interface accepts RT64's existing VI state. GL presentation uses
  its visibility, gamma, and field-address calculations. Deterministic host GLES
  tests cover blanking by status and horizontal region, gamma toggling, odd-field
  origins, missing framebuffers, and restoration of a previously rendered image.
  Arbitrary VI stride/scaling and framebuffer feedback remain separate work.
- An explicit color-image readback API now passes host GLES tests for RGBA16/32
  packing, top-to-bottom row order, partial/unaligned ranges, format changes and
  flushing pending batches. DK64's framebuffer-copy routine now synchronizes its
  source through this API before executing the original CPU copy and morphing.
  Host screenshots show the blurred pause background and successful resume;
  native Vita3K readback remains subject to the limitation below.
- Framebuffer texture loads now retain immutable GPU snapshots and TMEM byte
  provenance. Compatible RGBA16/32 subregions sample those snapshots directly;
  mixed or reinterpreted TMEM layouts materialize the captured pixels on demand.
  Host tests cover split load/render tiles, bank/row mapping, snapshot lifetime,
  partial RAM changes, and reuse of framebuffer memory as texture data.
- Changed RAM bytes are merged into the GPU color image without reading the GPU
  image back to the CPU. RGBA16 half-pixel writes preserve the other byte, and
  RGBA32 writes preserve untouched channels. The Vita diagnostic visibly shows
  the expected red/cyan result in Vita3K/Vulkan. All four host integration checks
  pass; a separate 60-second host GL intro run completed 1,727 graphics tasks
  without simulated game inputs and rendered the colored character intro.
- The static recompiler's store helpers and native ROM/save/patch/RSP DMA writers
  now record writes to watched framebuffer pages. The renderer consumes byte
  masks at graphics boundaries, so same-value stores also replace GPU pixels.
  Tests cover KSEG aliases, every unaligned store offset, bulk writes, watch
  lifetime, and concurrent collection. All five host integration checks pass.
  A Vita3K/Vulkan diagnostic using the runtime collector changes cyan RGBA16
  pixels to RGB (0,24,255) by writing zero over a RAM byte already equal to zero;
  the native screenshot measures (0,24,254), within JPEG rounding. A 60-second
  host GL intro run with tracked generated stores completed 1,639 graphics tasks
  without simulated game inputs. These are completion counts, not a controlled
  performance comparison. The rebuilt Vita game also passed 6,000 graphics tasks
  through the DK Rap intro in Vita3K/Vulkan, with no controller button transitions
  and unchanged existing saves. One inspected scene showed 14 presentation FPS;
  this is not a physical-Vita performance measurement.
  Mixed-layout and format-reinterpreted texture loads can still require CPU
  readback; native writers outside the notified paths must
  supply write notifications as well.
- Color images and depth surfaces now have independent lifetimes. Equal-size
  color buffers that use the same depth address share its depth values, including
  across presentation and color-allocation replacement. Changing depth addresses
  preserves color and previously written depth. Host regressions cover switches,
  partial clears, and CPU color updates. The Vita3K/Vulkan diagnostic displays
  red/blue above magenta/yellow as expected. This requires vitaGL built with
  `STORE_DEPTH_STENCIL=1`; the previous build discarded depth between scenes.
  Explicit rectangle draws implement far-depth clears because the tested
  depth-only `glClear` path did not perform the partial clear correctly.
  All five host integration checks pass; a 60-second host intro run with the
  updated renderer completed 1,683 graphics tasks without simulated input.
  The rebuilt Vita game also passed 2,280 graphics tasks through the intro in
  Vita3K/Vulkan with the new depth storage enabled.
  Different-size depth views and general color/depth aliasing still need work.
- Overlapping RGBA16/32 color images now preserve N64 bytes across changes to
  their address, dimensions and format using GPU copies. Recorded and observed
  CPU writes propagate to every overlapping view; complete CPU overwrites let
  texture loads use RAM again. Cache retirement retains a complete view for
  subsequent reads and immutable snapshots. Host regressions include odd byte
  addresses, addresses above 16 MiB, stride/format changes and split-view cache
  pressure. The split-view test failed before the retirement fix and passes now.
  All five host integration checks pass with AddressSanitizer and Mesa softpipe.
  In Vita3K/Vulkan, the diagnostic preserves an original red/blue snapshot beside
  an overlapping red/blue/green result after an RGBA16-to-RGBA32-to-RGBA16 round
  trip. All 128 screenshot cell centers match, allowing a maximum JPEG channel
  error of 7. The native test also caught a shader byte-selection error missed
  by Mesa; arithmetic selection fixes the observed alternating-pixel corruption.
  The rebuilt game passed 7,080 graphics tasks in Vita3K/Vulkan without controller
  button transitions or save changes. Matched 60-second host llvmpipe intro runs
  with no added presentation delay completed 1,722 tasks with the published
  renderer and 1,723 with these changes. This shows no meaningful throughput
  difference for that workload; it is not a physical Vita performance result.
  Requests spanning several views without one complete containing view, mixed
  TMEM layouts that need CPU readback, and color/depth aliasing remain limited.
- CPU-written RGBA16/32 framebuffers can now be presented without a preceding
  RDP draw. Host tests cover both formats, later CPU edits, byte readback, cache
  pressure and out-of-range scanout. The separate Vita3K/Vulkan diagnostic shows
  yellow above cyan, then yellow above magenta after a CPU update; both byte
  readbacks exactly match guest RAM. Fully CPU-owned ranges avoid GPU readback.
  This does not fix GPU-owned readback in Vita3K or establish the original boot
  logo's timing. Interlaced scanout whose width differs from its RAM stride still
  needs implementation.

## Runtime validation

Startup requests the same clock frequencies as Ghostship's Vita port: CPU 444 MHz,
bus/GPU 222 MHz and GPU crossbar 166 MHz. Requests only raise clocks below those
values, preserving higher device settings. Vita3K reports the requested values
after initialization; an emulator does not measure the physical performance
benefit. The diagnostic clock report is absent when diagnostics are disabled.

The upstream `timing_fixes.c` audio-wakeup race fix is also required on Vita.
The original delayed timer allowed the audio thread to wait for a task that was
never submitted while graphics continued. The Vita hook now sends message 5
directly only when the audio queue is empty, matching the desktop patch. A
30-second host GL run with a deliberate 33 ms presentation delay delivered only
13 audio buffers before the fix and 202 with it. Native Vita3K traces stopped
after one to three buffers before the fix; the updated run passed 1,080 audio
task submissions. Audio buffers still run empty during expensive graphics work,
so this fixes scheduler liveness without establishing smooth playback under
load. The host probe now reports audio delivery and requires at least 120 audio
buffers as well as 120 graphics tasks, preventing startup-only audio from being
mistaken for a successful integration run.

The [upstream patch audit](VITA_UPSTREAM_PATCH_AUDIT.md) accounts for all 162
compiled upstream replacements and the additional disabled source definition.
The MIPS objects, linked ELF and strict-recompiler registration table agree on
that inventory. It is an ongoing semantic audit, not a claim that every desktop
replacement is active or appropriate on Vita; unresolved rows remain explicit.
`tools/audit_vita_patches.py` checks inventory/source drift and the generated
report, and can fail if unresolved rows remain.

Boot also retains the original CPU-drawn red Nintendo logo. Native initialization
previously reached the main loop and reset logo mode before its 60-VI unblanking
point. The startup hook now yields until the logo is unblanked, plays the original
sound with private guest arguments and holds the image for 1.4 seconds. The
original 0.3-second initialization timer and queue capacities remain. A host
capture verifies the formerly missing logo; helper tests cover timing, sound
ordering and caller state. The quiet native Vulkan build reaches the normal intro,
but the short logo window and physical-device audibility still need confirmation.

Native gameplay hooks now restore the five Helm temporary completion flags for
an eligible existing save and remove lingering cutscene controllers before a new
one is spawned. Cleanup handles both deferred deletion and immediate actor-list
compaction. A narrowly scoped instruction patch removes the deferred-free
routine's uncached corruption check while retaining its frees. Fairy-photo
capture synchronizes the selected framebuffer before the original CPU tile
loop, keeping the original allocation and texture-only fallback. Rabbit speed
and Krazy Kong Klamour durations also carry the upstream lag adjustments; Klamour
retains its original byte-sized difficulty value and expands it at the 16-bit
timer consumer to avoid overwriting adjacent overlay data.
An additional hook preserves the upstream opaque-black cover for a stopped
transition beyond its completion threshold, with a private guest argument area.
Framebuffer fades and wipes retain the fractional half-lag sampled at function
entry. Their integer progress truncates after arithmetic; radial and clock
updates retain the upstream double arithmetic. The original CPU framebuffer,
pause blur and deferred release remain in use.

The host checks include caller-register/stack preservation, save
eligibility, actor selection/compaction, actual generated deferred-free and photo
capture routines, framebuffer effect completion/cleanup, and minigame timing
boundaries. The removed-check, photo and fractional-transition tests failed
before their hooks were added. Photo testing isolates the game's
color conversion. Full affected-save, repeated Owl Race, fairy-photo and
minigame playthroughs remain required; these focused checks do not replace them.
A subsequent 85-second host GLES run reached Training Grounds, displayed the
original blurred pause snapshot, resumed and continued movement/audio. The quiet
ARM VPK also booted and rendered the DK Rap in Vita3K with Vulkan. Individual
transition pixels and physical Vita framebuffer readbacks remain unverified.

The zipper snapshot patches were reviewed as one dependency chain. Vita retains
the original synchronized CPU copy, six-column alpha seam and DECALRGBA texture
combiner. The desktop GPU snapshot, disabled CPU morpher and alpha-ignoring
combiner are a different linked implementation. Generated-code checks cover
single snapshot initialization, scene handoff, resource release, frame reuse and
the seam's pixel mutations. A no-op morpher negative control fails. A focused
GLES check verifies the original alpha-coverage mode without primitive-color
tinting, both with and without batching. Full physical zipper animation and
border appearance still require gameplay validation.

The Galleon sky hook also carries the upstream fix for chunks 0, 6, 7 and 8,
preserving their existing style/tint parameters. Its generated-code regression
failed before the one-instruction patch and covers chunk, override and view
variants afterward. Other maps keep their original background dispatch and
inclusive fill-cycle bounds. Direct comparisons against the compiled upstream
skybox functions found matching blend/textured-sky calculations at 4:3 after
desktop matrix tags were removed. Helpers are modeled in those comparisons;
actual Galleon, race and bonus sky pixels still need gameplay validation.

Lighting keeps the original 16-entry input table and 12-record processed-light
limit. Its zero-direction, zero-radius and shadow-denominator guards already
exist in the original code. Differential checks against the compiled upstream
replacements matched all 768 cases, including degenerate inputs and selection
limits. The actor draw routine also matched 432 cases with the desktop distance
floor disabled; enabling that floor changed the expected control case. Vita
retains authored actor distances. These comparisons model helper functions and
do not establish complete rendered lighting or distant-model appearance.

Portal traversal, chunk scissors and distant-screen visibility also remain
paired with the original 4:3 projection. The original traversal can mark a
billboard visible without loading the adjoining chunk. Desktop patches remove
that path and instead update all distant screens; mixing only half of that
change suppresses billboards in the constructed comparison. The review covers
192 traversal pairs using the real rectangle helpers, 96 world-draw/scissor
pairs and 72 enabled distant-record calculations. Geometric predicates and
geometry-draw helpers are modeled; real portals, multiplayer views and distant
objects still require gameplay validation.

Two screen-overlay fixes also apply at the native aspect ratio. Cutscene fades
now emit upstream's uniform primitive alpha instead of alternating normal and
doubled alpha at the original quad corners. Rap static uses the current viewport
for its scissor and stripe width, fixing noise outside a contracted view while
preserving the game's RNG. Both failed targeted pixel regressions before their
hooks and pass through the real Fast F3DEX2 interpreter and GLES renderer afterward.
Command streams also match the compiled upstream replacements in 2048 fade and
56 static cases. Underwater tint and sandstorm geometry remain under review.

The deferred asset-loading audit retains the original 192-entry job array, DMA
descriptor array and completion queue as one unit. A generated-code regression
fills all 192 entries with mixed raw/compressed jobs, checks the overflow guard,
cache/direct-pointer and immediate paths, and verifies cleanup ownership. Its
DMA and codec are modeled. The generated music loader also passes four-channel,
even/odd size and scratch-lifetime checks: the original already rounds the
difference between adjacent music-table offsets to an even size. These results
do not justify importing the desktop 1024-entry queue or static music workspace.
Host-only asset counters observed a peak of 98 jobs and 92 completions during
boot, menu, story and Training Grounds runs; later dense maps remain untested.

Those runs also exposed an invalid timer notification to queue address zero.
The runtime now suppresses null-queue notifications, matching the original
libultra interrupt handler. It also finishes timer reads and periodic rearming
before publishing a completion: DK64 uses one-shot timers on the caller's stack,
which may be reused as soon as the receiver wakes. A deterministic regression
using the actual timer worker fails both with the old code and with only the
null-queue guard, then passes with the ordering fix. Three subsequent 70-second
host runs reached Training Grounds and completed about 1,990 graphics tasks and
2,020 audio buffers each without the crash. The precise timer instance behind
the observed queue-zero event was not established. These bounded runs are not
proof of complete timer concurrency correctness or physical-Vita stability.

The quiet Release package with the timer fix reaches the animated N64 logo in
Vita3K/Vulkan, but later suffers severe slowdowns and emulator memory-protection
faults. A comparison with the archived pre-timer quiet build reproduced the
same seven fault addresses in the same order. The diagnostic build also reports
faults while continuing to submit graphics tasks. This is an unresolved native
emulator limitation, not a clean runtime pass; the evidence does not establish
the underlying cause. No game input was injected and save hashes stayed intact.

Follow-up renderer measurements found zero GPU readbacks during the measured
intro pause. Three alias copies and a handful of CPU memory merges accounted
for about one second, while shader creation stalled for much longer. The build
now enables vitaGL's persistent shader cache in the game's data directory.
For 29 matched shader sources, the instrumented Vita3K/Vulkan compile/load path
took 22.805 seconds with an empty cache and 0.008565 seconds on the warm run.
Cold, warm and mixed cached/uncached runs rendered the DK Rap; removing one
fragment entry produced exactly one cache miss and regenerated identical bytes.
The temporary renderer counters and timers are not part of the release source.
These measurements concern shader creation, not average game FPS. First-use
compilation, ongoing draw cost, audio underruns and Vita3K memory-protection
warnings remain separate issues.

Earlier POSIX-semaphore runs intermittently reported:

```
No threads left to run: current=80761430 id=3 priority=10 queue=807659e8
```

At that point the recorded game exception code and shutdown flag were zero, and
the game's watchdog was 200. The VI queue starvation was independently reproduced
and fixed; the remaining scheduler failure must not be conflated with it. The
CPU vertex/depth/lighting corrections have passed host tests and the Vita build.
The 240p build centers the logo in Vita3K. One POSIX-semaphore run stopped producing
game tasks after 206 SP submissions while VI presentation continued. A scheduler
history later captured a blocked game thread resuming without a corresponding
signal, then inserting itself into its own wait queue. A direct Vita kernel
semaphore path has since advanced through more than 14,000 completed graphics tasks
without that scheduler failure; several other runs also progressed through the
intro. This is a promising isolation result,
not yet a complete stability proof. A separate 40,000-handoff diagnostic passed
with both semaphore implementations, so the precise cause of the erroneous wake
has not been established.

That longer native run eventually stopped at task 14,419 because the fast renderer
threw on a model-matrix stack overflow. RT64's existing RSP instead saturates pushes
at 32 entries and clamps pops at the base entry. The fast path now follows that
behavior, with regression coverage for excess pushes/pops and forced matrices.
After the correction, an ASan host run completed 19,928 graphics tasks in 240
seconds. The corrected native run has passed 60,000 graphics tasks without that
failure (a later observation exceeded 172,000 tasks). The user also confirmed that
physical Enter presses open the main menu;
the input log confirms Start, D-pad, Z, and R mappings. Automated computer-use key
taps did not reproduce those held inputs reliably, so they are not input-failure
evidence for the port.

The character intro now has colored characters and a textured floor on Vita.
Performance is still scene-dependent. The emulator's presentation FPS is
insufficient to judge game progress. Slow rendering also built
up thousands of obsolete VI presentation actions; the Vita path now keeps the
latest scanout registers behind one pending presentation action. Guest VI interrupt
delivery is unchanged. This coalescing passes a bounded-queue regression and a
host run now captures the correctly colored and textured character intro at task
480. The same shader generator and GLES draw path produce a clean host image.
Sampler-upload validation, matching host/Vita draw traces, and a small diagnostic
isolated the color issue to texture filtering with unused alpha. Disabling shader
fast math and protecting zero mask divisors did not fix it; both experiments were
reverted. Shader
programs are now looked up by their actual input state, avoiding GLSL string
generation for every triangle. Uniform locations and fixed sampler assignments
are cached when each program is linked.

The Vita configuration now carries the desktop port's minimum-lag guard and
frame-deadline logic, including the VI callback's deferred presentation. Without
these, the host probe observed zero/one-frame lag and an Adventure story sequence
that remained on its first map. With the corrections it has advanced through the
timed story maps. A 420-second ASan host run completed 11,820 graphics tasks,
finished the story, and recorded player movement in DK's house. Reloading that
save entered Training Grounds and completed another 2,977 tasks in 100 seconds,
with movement after its opening cutscene. These are host runtime/save checks,
separate from the native renderer and physical-input checks.
The implementation uses guest-memory accessors in `timing.cpp`
and preserves the desktop's arcade, fast-load, DKTV, rap, and story intervals.
Focused tests cover minimum lag, cooperative waits, and interval selection;
the generated branch conditions and delay slots were also checked.

The separate native Adventure probe also completed the opening story and reached
player control in DK's house, exceeding 7,080 graphics tasks. Its guest-state log
records cutscene/autowalk both zero and changing horizontal positions and jump
height, and native screenshots show the textured interior. Shader compilation
causes substantial first-use stalls; observed renderer intervals vary by scene,
including roughly 30-48 ms in some story intervals and about 81 ms in a later one.
These observations are not a controlled benchmark or physical Vita results.
Restarting the native probe with its existing 2 KiB save loaded the file into
Training Grounds, where the logs and native screenshot show movement and swimming
after the cutscene. The regular app's save and backup remain byte-identical to the
copies taken before the separate probe.

The program cache excludes depth/blender state that does not change the generated
fragment shader, and shares the fill program independently of the previous mux
and texture state. A regression compares generated GLSL for every colliding key
while toggling each mode bit across cycle, texture, fog, and fill combinations.
The identical vertex stage is also compiled once per renderer and shared by all
combiner programs, avoiding repeated runtime compilation of that stage.

Float uniform values are now retained per combiner program. Unused float uniforms
and unchanged values no longer reach the GL API. The host graphics regression
checks color changes (including zero), separate values for the same uniform
name in different programs, texture dimensions/shift/clamp/mask/mirror changes,
and switches through snapshot/presentation programs. A controlled 32-draw
sequence fell from 352 uniform calls to zero after its initial draw while
preserving the expected pixels. Two 35-second host Adventure runs submitted
118,887/118,938 draws and 2,044,634/67,374 uniform calls respectively, a 96.7%
reduction per draw. The optional `PROBE_GL_AUDIT=ON` linker wrappers record these
calls; they are excluded from Vita builds and must not be used for frame-time
benchmarks.

VitaGL also marks all shader constants dirty on each `glUseProgram`, including
redundant binds. Draw submission now checks the actual current binding first.
With warm shader caches and normal controls, the measured Vulkan renderer
intervals ending at tasks 600/720/840/960 fell from 125.70/126.62/127.54/124.65 ms
to 121.11/122.14/122.82/120.70 ms. Uniform caching alone did not materially
improve those intervals. This is a modest result from one emulator comparison,
not a physical-Vita FPS claim; demanding scenes and audio still need work.

Further native stage sampling identified texture decoding and TMEM loading as
the dominant work in demanding intro scenes. RT64 now retains ordinary decoded
textures in a 2 MiB cache across reloads, with exact layout/content comparison
after hashing. Framebuffer-backed and mixed loads bypass it. Aligned ordinary
TMEM uploads use word transfers with the original bank split and row swap;
the existing byte path handles the remaining cases. The complete source span
is validated before arithmetic is narrowed to the 32-bit target.

The renderer's core tests now run alongside the other host checks under ASan.
They compare repeated cached decodes with fresh decodes across formats and
palettes, force content/layout index collisions, exercise eviction and retained
references, reject oversized cache entries and invalid transfer spans, and
compare aligned transfers with equal logical bytes at unaligned addresses.
GPU-only and mixed-TMEM tests check that CPU cache reuse cannot bypass provenance.
The stage sampler was temporary and has been removed from the source.

With the sampler removed and the same warm shader-cache configuration, the
renderer intervals ending at tasks 600/720/840/960 measured
45.82/46.13/46.25/46.41 ms, versus 121.11/122.14/122.82/120.70 ms before the
texture changes: roughly 62% less renderer processing time in those intervals.
This is an emulator renderer measurement, not physical-Vita FPS. A 70-second
host Adventure run reached Training Grounds and completed 1,985 graphics tasks
and 2,012 audio buffers. The extended sampled native attract-mode run exceeded
19,000 graphics tasks. Native game inputs were not injected and save hashes
were unchanged. Broader gameplay, audio quality and hardware performance remain
open validation work.

CPU framebuffer requests share an ordered graphics-queue producer with display
lists. A two-thread regression reproduces a readback overtaking an earlier task
with implicit producers, then verifies the shared producer preserves their order.
The CPU renderer also defers SP completion until `send_dl` finishes reading guest
commands, vertices and matrices. This prevents the completion notification from
allowing the game to reuse those inputs while decoding is still in progress.

A non-sanitized host run exposed the original audio loop's direct `AI_LEN` read
at `0x80601F0C` (`0xA4500004`). The Vita recompiler configuration now replaces it
with `osAiGetLength_recomp`, as the desktop patch does. Reduced-memory 64-bit host
runs reserve the rest of the 512 MiB physical-address range as inaccessible guard
space: ASan does not automatically track the bounds of a raw mmap allocation,
so the earlier 32 MiB mapping could let an invalid access land in neighboring
mapped memory. The guarded ASan runtime subsequently completed 3,578 tasks in
120 seconds, including pause/resume in the capture-only probe.

The llvmpipe host run completed 5,282 graphics tasks in 180 seconds with rendered
pause/resume captures. One earlier fast-host run faulted during map loading;
that fault was not reproduced by the instrumented reruns or the later run with
deferred SP completion. Its exact cause was not established, so longer stability
testing remains necessary.

`DK64_VITA_PROFILE_FUNCTIONS=ON` enables temporary function-stack samples and
message-queue tracing in `progress.log`. The current release build disables this
option. It must be disabled for performance measurements. `RT64_FAST_VALIDATE_UPLOADS`
enables byte comparisons of new vitaGL texture allocations and is also disabled
by default.
`DK64_VITA_TRACE_RENDERER=ON` independently captures startup and task-480 draw
details and decoded textures, without instrumenting all recompiled functions.

`platform/host_probe` builds the same generated Vita game against the native Linux
runtime, with the 32 MiB guest memory limit, KSEG1 aliases, and fair graphics queue.
`PROBE_GL=ON` enables the same raster backend through GLES2 and offscreen EGL. The
`dk64_runtime_probe ROM_DIRECTORY [seconds] [presentation_ms] [batching:0|1] [adventure|pause] [capture]`
process deliberately exits at its time bound; it does not validate graceful
shutdown or audio output. The optional `adventure` script supplies input through
the runtime callback and logs maps, cutscenes, and player coordinates; it does
not exercise Vita3K keyboard input. Its Dockerfile extends `dk64-vita-build` with Mesa. With ASan
under this machine's x86 emulation, use `GALLIUM_DRIVER=softpipe`; llvmpipe's LLVM
JIT failed during EGL context creation before renderer initialization.
`PROBE_ASAN=OFF` permits llvmpipe for faster game-image checks on this host; retain
the sanitizer checks separately. The final `capture` argument bypasses EGL even
in a GL-enabled build, allowing the same binary to test the guarded guest runtime
without the software rasterization cost. The `pause` script captures paused and
resumed frames when GL is enabled.

The optional `DK64_VITA_SCRIPTED_INPUT=ON` build produces
`DK64AdventureProbe.vpk` (`DK64RT001`) using the same controller-callback script.
It uses `ux0:data/dk64recompiled-probe/` for its ROM, saves and logs, so it cannot
modify the regular port's saved game. It does not test physical input delivery.
The regular `DK64Recompiled.vpk` is produced with this option **OFF** (the default).
`DK64_VITA_SCRIPTED_PAUSE=ON` also exercises pause/resume in the native probe.

## Work remaining

- Implement general framebuffer-to-texture feedback and the remaining VI
  stride/scaling behavior; validate depth/blender
  and texture corner cases against game output.
- Expand DK64 validation beyond the opening story and Training Grounds, including
  pause screens, camera/framebuffer effects, minigames, and longer save round trips.
  Dynamic code mods remain disabled in the Vita build.
- Extend unsupported microcodes and extended GBI commands as required by other
  recompilation projects. A working DK64 path does not establish their coverage.
- Measure batching on the same workload and reduce remaining submission overhead.
- Continue checking gameplay, audible audio quality, input, saves, and framebuffer
  effects separately.
  Vita3K results do not establish performance or correctness on physical hardware.

The supplied ROM remains local and is excluded from Git and distribution artifacts.
No guarantee is made that every RT64-based game becomes portable automatically;
microcode coverage, runtime dependencies, memory use and performance are per-game
validation requirements.

## Renderer validation build

```sh
docker build --platform linux/amd64 -t dk64-vita-build -f platform/vita/Dockerfile .
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build \
    cmake -S . -B build/vita -DDK64_VITA=ON -DDK64_VITA_GAME=OFF -DCMAKE_BUILD_TYPE=Release
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build \
    cmake --build build/vita -j8
```

This builds `build/vita/platform/vita/rt64_fast_smoke.vpk`, a renderer diagnostic,
not the game. It writes logs and a framebuffer readback to `ux0:data/rt64-fast/`
and exits on Circle. The portable tests can be built directly from `lib/rt64` with
`-DRT64_FAST=ON -DRT64_FAST_TESTS=ON` and run as `src/fast/rt64_fast_tests`.

The container builds vitaShaRK at the verified latest upstream commit
`df24065e65098b2d1ac533760109ad4367573f28` and then vitaGL at
`cd3791e29ff7f1c0ab349f12c7231f4871ce6a75`, always with `NO_SPLASHSCREEN=1`
and `STORE_DEPTH_STENCIL=1`, with `HAVE_SHADER_CACHE=1` for persistent shader
binaries. The game selects
`ux0:data/dk64recompiled/shaders-cd3791e-df24065-pair/`; the scripted probe uses
its own data directory. vitaGL creates the versioned vertex/fragment
subdirectories. Bump this cache namespace in `platform/vita/main.cpp` when
changing the pinned libraries, compiler options or semantic binding mode.
Rebuild the Docker image after updating these build
options. When reusing a build directory, remove its generated `DK64Recompiled`
and `rt64_fast_smoke` executables inside `<build-directory>/platform/vita/`
before building again to force linking against
the rebuilt SDK libraries; CMake does not track changes to libraries passed by
name with `-l`.
The GLSL translator requires vitaShaRK's normal compiler extensions (including
`bit_cast` helpers). Supply the decrypted shader compiler at
`ur0:/data/libshacccg.suprx` inside Vita3K's configured filesystem root. Select
**Vulkan**, including any per-application renderer override.

## Game build

After generating the decompressed US ROM and host tools as described in
`BUILDING.md`, run:

```sh
cmake -S lib/N64ModernRuntime/N64Recomp -B build/host-recompiler -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-recompiler --target N64RecompCLI -j8
cp build/host-recompiler/N64Recomp ./N64Recomp
./N64Recomp us.vita.toml
./RSPRecomp n_aspMain.toml
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build \
    cmake -S . -B build/vita -DDK64_VITA=ON -DDK64_VITA_GAME=ON \
    -DDK64_VITA_PROFILE_FUNCTIONS=OFF -DCMAKE_BUILD_TYPE=Release
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build \
    cmake --build build/vita -j8
```

`us.vita.toml` preserves the base recompiler settings and adds the required lag,
frame-deadline, cooperative-wait, and 240p Nintendo-logo behavior from the desktop
game's patches. The mode patch changes the instruction at `0x805FB980` from loading 2 to
loading 1; the game then stores that value to its byte-sized mode field. Generated
files go to `build/RecompiledFuncsVita`, separately from desktop output.

The game VPK is `build/vita/platform/vita/DK64Recompiled.vpk` (`DK64RE001`). It
contains no ROM. Put the original, big-endian US ROM at
`ux0:data/dk64recompiled/DK64.z64`. Saves use `ux0:data/dk64recompiled/saves/`;
`progress.log` records startup milestones and fatal errors.

Current controls: left stick for movement, Cross for A, Square for B, L for Z,
R for R, Start for Start, D-pad for D-pad, and right stick for C buttons.

### Hardware test package without diagnostics

After generating the game sources, use a separate build directory and a vitaGL
image without shader or file logging:

```sh
docker build --platform linux/amd64 -f platform/vita/Dockerfile \
    --build-arg VITAGL_SHARK_LOG=0 --build-arg VITAGL_LOG_ERRORS=0 \
    -t dk64-vita-build-quiet .
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build-quiet \
    cmake -S . -B build/vita-hardware -DDK64_VITA=ON \
    -DDK64_VITA_DIAGNOSTICS=OFF -DDK64_VITA_PROFILE_FUNCTIONS=OFF \
    -DDK64_VITA_TRACE_RENDERER=OFF -DDK64_VITA_SCRIPTED_INPUT=OFF \
    -DDK64_VITA_SCRIPTED_PAUSE=OFF -DRT64_FAST_VALIDATE_UPLOADS=OFF \
    -DCMAKE_BUILD_TYPE=Release
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build-quiet \
    cmake --build build/vita-hardware --target DK64Recompiled.vpk-vpk -j6
```

For the optional vitaGL readback speedhack, add
`--build-arg VITAGL_READBACKS_SPEEDHACK=1` to the image build and
`-DRT64_FAST_READBACKS_SPEEDHACK=ON` to CMake. Use a separate image tag and build
directory when comparing variants. Both settings are needed: RT64 normally calls
`glFinish` before readback, which would retain a hard wait even with vitaGL's flag
enabled. The opt-in build omits that wait and can return earlier frame data.
Physical Vita tests must establish the performance and visual tradeoff.

The package is `build/vita-hardware/platform/vita/DK64Recompiled.vpk`. It uses
normal controls and saves, removes the frontend log files, diagnostic counters,
audio peak scans and debug sections, and rejects configurations that enable
scripted inputs or instrumentation. The image retains `NO_SPLASHSCREEN=1`,
`STORE_DEPTH_STENCIL=1` and the
pinned vitaShaRK revision. The ROM and `ur0:data/libshacccg.suprx` shader compiler
must be supplied separately on the Vita. Removing diagnostics does not change the
port's experimental compatibility status.

Known validation issue: Vita3K presents the diagnostic correctly, but vitaGL's
direct CPU `glReadPixels` path returns black for its offscreen framebuffer. Use
Vita3K's native screenshot capture when checking presentation. CPU framebuffer
readback remains a separate compatibility requirement.
The native game probe also returned zero colored pixels for all three 320x240
readbacks while continuing through pause/resume and gameplay. This confirms the
limitation affects game framebuffer copies as well as the small diagnostic; it
does not establish correct pause backgrounds on Vita3K or physical Vita hardware.

The installed macOS Vita3K run reports memory mapping as Disabled. The available
local Vita3K source disables Vulkan memory mapping on Apple platforms and gates
GPU surface synchronization on it (`renderer/src/vulkan/renderer.cpp` and
`surface_cache.cpp`). This is consistent with the black CPU readback, but the
local source revision differs from the installed app, so it is not an exact-binary
root-cause proof. A GPU transfer call in that source also falls back to guest-memory
copies when mapping is disabled; changing readback APIs alone is unlikely to fix
that emulator path.
