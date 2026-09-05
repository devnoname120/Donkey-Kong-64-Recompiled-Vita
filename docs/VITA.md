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
  flushing pending batches. It is not yet connected to DK64's CPU framebuffer
  consumers, and native Vita3K readback remains subject to the limitation below.

## Runtime validation

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
`dk64_runtime_probe ROM_DIRECTORY [seconds] [presentation_ms] [batching:0|1] [adventure]`
process deliberately exits at its time bound; it does not validate graceful
shutdown or audio output. The optional `adventure` script supplies input through
the runtime callback and logs maps, cutscenes, and player coordinates; it does
not exercise Vita3K keyboard input. Its Dockerfile extends `dk64-vita-build` with Mesa. With ASan
under this machine's x86 emulation, use `GALLIUM_DRIVER=softpipe`; llvmpipe's LLVM
JIT failed during EGL context creation before renderer initialization.

The optional `DK64_VITA_SCRIPTED_INPUT=ON` build produces
`DK64AdventureProbe.vpk` (`DK64RT001`) using the same controller-callback script.
It uses `ux0:data/dk64recompiled-probe/` for its ROM, saves and logs, so it cannot
modify the regular port's saved game. It does not test physical input delivery.
The regular `DK64Recompiled.vpk` is produced with this option **OFF** (the default).

## Work remaining

- Implement framebuffer feedback/readback integration and the remaining VI
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
`cd3791e29ff7f1c0ab349f12c7231f4871ce6a75`, always with `NO_SPLASHSCREEN=1`.
The GLSL translator requires vitaShaRK's normal compiler extensions (including
`bit_cast` helpers). Supply the decrypted shader compiler at
`ur0:/data/libshacccg.suprx` inside Vita3K's configured filesystem root. Select
**Vulkan**, including any per-application renderer override.

## Game build

After generating the decompressed US ROM and host tools as described in
`BUILDING.md`, run:

```sh
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

Known validation issue: Vita3K presents the diagnostic correctly, but vitaGL's
direct CPU `glReadPixels` path returns black for its offscreen framebuffer. Use
Vita3K's native screenshot capture when checking presentation. CPU framebuffer
readback remains a separate compatibility requirement.

The installed macOS Vita3K run reports memory mapping as Disabled. The available
local Vita3K source disables Vulkan memory mapping on Apple platforms and gates
GPU surface synchronization on it (`renderer/src/vulkan/renderer.cpp` and
`surface_cache.cpp`). This is consistent with the black CPU readback, but the
local source revision differs from the installed app, so it is not an exact-binary
root-cause proof. A GPU transfer call in that source also falls back to guest-memory
copies when mapping is disabled; changing readback APIs alone is unlikely to fix
that emulator path.
