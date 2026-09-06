# Vita framebuffer readback validation

The official Linux ARM64 Vita3K/Vulkan build can return correct pixels through
vitaGL from the first GPU read when using **external-host** memory mapping on the
tested Mesa llvmpipe device. The matched DK64 run also shows correct first and
second pause backgrounds. Double-buffer mapping retains first-read failures;
disabled mapping returns black in the tested GPU reads. The working configuration
is a validation route for this emulator/driver pair, not a game or renderer
workaround and not physical-Vita proof. See the [mapping control](#external-host-mapping-control)
and [matched game comparison](#matched-dk64-pause-comparison).

## Initial controlled comparison, September 6, 2026

Both runs used the same diagnostic VPK, emulator executable, isolated filesystem,
firmware, shader compiler and Vulkan software device. Saved emulator configurations
differed only in `memory-mapping`. Neither run used the readback speedhack.

| Input | Value |
| --- | --- |
| Emulator | Vita3K v0.2.1 `4074-496939b6` |
| Full source revision | `496939b602703951277263c7b3e60a9ae36879c1` |
| Official Linux ARM64 artifact | [`vita3k-496939b6-linux-arm64`, artifact 9043818590](https://github.com/Vita3K/Vita3K/actions/runs/31333950952) |
| Artifact ZIP SHA-256 | `771616d91a33e4f25c0a504f0e66b69a5340020629d2413eb6418266edf5cb65` |
| Host | Ubuntu 24.04 ARM64 container, Xvfb |
| Renderer | Vulkan, llvmpipe (LLVM 20.1.2, 128 bits), Mesa 25.2.8 |
| Surface synchronization | Enabled in both runs (`disable-surface-sync: false`) |
| Mapping modes | `disabled`, `double-buffer`; each confirmed in the emulator log |
| vitaGL | `cd3791e29ff7f1c0ab349f12c7231f4871ce6a75` |
| vitaShaRK | `df24065e65098b2d1ac533760109ad4367573f28` |
| vitaGL options | `NO_SPLASHSCREEN=1`, `STORE_DEPTH_STENCIL=1`, `HAVE_PTHREAD=1`, `HAVE_GLSL_TEXTURE_SIZE=1`, `HAVE_SHADER_CACHE=1`, `READBACKS_SPEEDHACK=0` |
| Diagnostic CMake | Release; `DK64_VITA=ON`, `DK64_VITA_GAME=OFF`, `DK64_VITA_RUNTIME=OFF`, `RT64_FAST_READBACKS_SPEEDHACK=OFF` |
| Diagnostic VPK SHA-256 | `8d60e7f17956712c999ded6af493abcd740de9eeeeb29b024474a82f37354c2b` |
| Diagnostic eboot SHA-256 | `e5103bbc1d39310a00a9d610203a7a4b7b77548da8af1d00afa24515b46b1690` |

`DK64_VITA_RUNTIME=OFF` selects the diagnostic's explicit CPU-write notifications;
the new readback images contain only GPU draws and do not use that fallback.
The SDK archive's `glReadPixels` contains its scene reset and `sceGxmFinish` calls;
RT64 also retains its normal `glFinish` before reading.

| Check | Mapping disabled | Double-buffer mapping |
| --- | --- | --- |
| Direct 320x240 `glReadPixels`, frame 2 | 76,800 black pixels | 76,800 black pixels |
| Same image, frames 3, 4, 30 | 76,800 black pixels each | 75,915 non-black pixels each; all three files identical |
| First 64x64 GPU-only `readFramebuffer` | 4,096 zero RGBA16 values | 4,096 zero RGBA16 values |
| Three following alternating GPU images | 4,096 zero values each | All 4,096 values match the current image each time; zero match the previous image |
| Existing CPU-only scanout reads | Both match guest RAM | Both match guest RAM |

The 64x64 pattern alternates top/bottom red/blue (`f801`/`003f`) and green/yellow
(`07c1`/`ffc1`) in N64 RGBA16 format. This tests the public RT64 readback path,
top-to-bottom packing and freshness against known colors, rather than treating a
non-black image as sufficient. No CPU image uploads initialize these targets.

Each run was bounded to 20 seconds and ended with timeout exit 124 after reaching
both CPU-only scanout checks. The double-buffer emulator log also contains
`Unhandled SIGSEGV` messages while the diagnostic continues; this is not a clean
emulator-log or general stability result.

## Source explanation and limits

Source at the installed macOS app's reported revision explicitly disables memory
mapping on Apple platforms in
[`renderer.cpp`](https://github.com/Vita3K/Vita3K/blob/496939b602703951277263c7b3e60a9ae36879c1/vita3k/renderer/src/vulkan/renderer.cpp#L684).
[`perform_surface_sync`](https://github.com/Vita3K/Vita3K/blob/496939b602703951277263c7b3e60a9ae36879c1/vita3k/renderer/src/vulkan/surface_cache.cpp#L1197)
returns immediately when mapping is disabled. This agrees with the controlled
Linux result and explains why the macOS run cannot validate this synchronization
path. The source is matched to the reported revision, not a disassembly of the
installed macOS executable.

The first-read behavior is consistent with the same revision's lazy surface
tracking:
[`protect_surface`](https://github.com/Vita3K/Vita3K/blob/496939b602703951277263c7b3e60a9ae36879c1/vita3k/renderer/src/vulkan/surface_cache.cpp#L61)
marks a surface as needing synchronization when the CPU first accesses its memory.
The callback only sets flags; synchronization is performed during a later
[`context submission`](https://github.com/Vita3K/Vita3K/blob/496939b602703951277263c7b3e60a9ae36879c1/vita3k/renderer/src/vulkan/context.cpp#L451).
This is a source-based explanation of the observation, not an instrumented trace
of that callback. Repeating a static capture had concealed the distinction between
current and stale pixels; the alternating-image check makes it explicit.

These results do not prove a correct first pause snapshot, transition capture or
fairy photograph, and do not establish physical Vita behavior. They also do not
test the hardware build's requested delayed-readback option. No emulator-specific
retry, dummy draw or graphics workaround was added to the game or RT64.

## DK64 pause and resume

The same Linux emulator and double-buffer mapping were then used with the actual
ARM DK64 game probe, normal synchronized readbacks, and copied probe saves. The
scripted title `DK64RT001` uses a separate data directory. Its renderer records
only the bytes returned by the game's own framebuffer requests: it does not make
additional reads that might prime the surface tracking.

A first, 180-second run reached Training Grounds, paused, resumed and continued
moving. Its first captured image contained the earlier Nintendo boot logo; the
following transition and pause reads were black. Non-black pixels alone therefore
do not demonstrate a correct game screenshot.

The next probe added a second pause cycle. It entered DK's house from Training
Grounds, completed both pause/resume cycles in that room and continued moving.
The game requested the same 320x240 RGBA16 address, `0x00049800`, for both pauses:

| Game-requested capture | Colored pixels | Observed content |
| --- | --- | --- |
| Earlier read at `0x00034000` | 5,278 / 76,800 | Stale Nintendo boot logo |
| Earlier read at `0x0004fa00` | 0 / 76,800 | Black |
| First pause at `0x00049800` | 0 / 76,800 | Black |
| Second pause at `0x00049800` | 72,030 / 76,800 | Room image used for the blurred pause background |

Vita3K's own Vulkan screenshots independently show the visible result:

| First pause | Second pause |
| --- | --- |
| ![First pause with black background](images/vita3k-pause-1.jpg) | ![Second pause with blurred room background](images/vita3k-pause-2.jpg) |

The second resume screenshot shows Donkey Kong in the room with the sharp gameplay
image restored. Logs record both `pause=1` to `pause=0` transitions and subsequent
player movement. This verifies repeated pause rendering in that scene; it does not
establish first-read correctness, fairy photography, general transition behavior,
audio quality or physical Vita compatibility.

The repeat probe VPK SHA-256 is
`beeb1b5b3ce42047a851bbf9d4140f22936da113e977cd17016e1214b000c052`;
its eboot SHA-256 is
`90f48eeeb1ae4a46ee0571e9514082049e06ad59dcd2a4e41de974884f170185`.
All four raw captures are 153,600 bytes. The repeat run reached at least 3,480
graphics tasks before its 180-second bound (exit 124). Native screenshots were
requested with F12 in the isolated Xvfb game window; gameplay input came from the
probe callback. The screenshots do not use the failing guest readback path.

Raw captures, screenshots, configuration, input-save hashes and logs are retained
locally in `build/vita3k-linux-control/adventure-double-buffer-repeat-result/`, with
`validation.json` recording the results. The original probe saves and existing
quiet hardware VPK retain their hashes. Under the quiet build's compiler flags,
the renderer's preprocessed source is byte-identical before and after adding the
probe-only recording block. No hardware game build was regenerated or deployed.

## External-host mapping control

A follow-up used the same official `4074-496939b6` executable and unchanged
`RT64F3001` diagnostic, with CPU optimizations enabled in both runs. The saved
configurations differed only in `memory-mapping`; Vulkan and surface
synchronization stayed enabled. The driver was again Mesa 25.2.8 llvmpipe,
reporting `VK_EXT_external_memory_host` and 4 KiB host-pointer alignment.

| Check | Double-buffer | External-host |
| --- | --- | --- |
| Direct frame 2 read | 0 / 76,800 colored pixels | 75,915 / 76,800 colored pixels |
| Direct frames 3, 4, 30 | 75,915 colored pixels each | All byte-identical to frame 2 and the double-buffer frame 3 |
| First 64x64 alternating-pattern read | 4,096 zero values | 4,096 / 4,096 match the current image |
| Three later pattern reads | All match the current image | All match the current image |
| Previous-image matches in successful pattern reads | 0 | 0 |
| CPU-only scanout checks | Both pass | Both pass |
| `Unhandled SIGSEGV` messages in this small diagnostic | 19 | 0 |

Both runs reached the final diagnostic checks and ended at the 20-second bound
(timeout exit 124). A `page-table` attempt selected that mode but aborted during
graphics initialization (exit 134), before producing any image. It is not a
working alternative in this tested setup.

The pinned source explains why these modes take different synchronization paths.
On non-NVIDIA Linux devices,
[`renderer.cpp`](https://github.com/Vita3K/Vita3K/blob/496939b602703951277263c7b3e60a9ae36879c1/vita3k/renderer/src/vulkan/renderer.cpp#L977)
sets `can_mprotect_mapped_memory` false for external-host mapping. The
[surface-creation path](https://github.com/Vita3K/Vita3K/blob/496939b602703951277263c7b3e60a9ae36879c1/vita3k/renderer/src/vulkan/surface_cache.cpp#L373)
then marks linear surfaces for synchronization immediately, instead of waiting
for their first CPU access to set that flag. The comparison changes the mapping
implementation too, so it is not an isolated instrumentation trace of that flag.
No emulator source or binary patch was needed or applied.

This route depends on the reported Vulkan extension and the platform/driver
branch. It must not be generalized to macOS, whose mapping is disabled by this
revision, or to other GPU/OS combinations without testing. Eager synchronization
has a cost; these captures are not a performance recommendation or hardware FPS
measurement. The isolated Linux profile now uses external-host mapping for
subsequent framebuffer validation.

Inputs, output hashes, configs, logs and raw images are in
`build/vita3k-readback-control/{double-buffer,external-host,page-table}-official/`;
`results.json` records the independent pixel counts. The source snapshot beside
those results is pristine and was used for inspection, not to replace the
official emulator.

## Matched DK64 pause comparison

The newer game probe was then run under both mappings with the same VPK, copied
input saves, Vulkan device, CPU optimizations and controller script. The game
source is `7edb974`, including both audio fixes. Its normal-readback diagnostic
VPK SHA-256 is `73f1f0911450ad4aac047c383604e12a37496184fb394e076af3375f50dec32f`;
the eboot hash is `b5d254c03b1c38185ea5d11c804933cd1b32a5f19cc3d89813886d0d1bc94256`.
`DK64_VITA_SCRIPTED_INPUT` and `DK64_VITA_SCRIPTED_PAUSE` are ON; audio capture,
function profiling, renderer tracing, map override and both readback speedhacks
are OFF. The separate title/data directory remains `DK64RT001` /
`ux0:data/dk64recompiled-probe`.

Both 180-second runs reached Training Grounds, completed two pause/resume cycles,
continued moving afterward and reported at least 5,160 graphics tasks. Only the
mapping field differed in the saved emulator configs. Scheduling and player
positions are not deterministic, so the game images are not compared byte-for-byte
across runs. The known-color diagnostic above provides that controlled pixel test.

| Game-requested image | Double-buffer colored pixels | External-host colored pixels and content |
| --- | ---: | --- |
| First transition, `0x59800` | 0 / 76,800 | 76,177; current save-selection menu |
| Next transition, `0x4FA00` | 0 / 76,800 | 47,901; intro view through a tunnel |
| First pause, `0x4FA00` | 0 / 76,800 | 76,208; current Training Grounds scene |
| Second pause | 0 / 76,800 at `0x2A200` | 76,525 at `0x4FA00`; updated Training Grounds scene |

Each recorded byte array is 153,600 bytes. The first pause reuses a previously
requested address, so its visible success alone would not prove first-use
synchronization. The matched double-buffer run still produces a black background,
and the separate diagnostic's untouched first surface reads correctly with
external-host mapping.

| First pause, double-buffer | First pause, external-host |
| --- | --- |
| ![Black pause background with double-buffer mapping](images/vita3k-mapping-double-pause.jpg) | ![Blurred Training Grounds with external-host mapping](images/vita3k-mapping-external-pause.jpg) |

![Gameplay restored after the first external-host pause](images/vita3k-mapping-external-resume.jpg)

These are Vita3K's own screenshots, requested with F12 in the isolated Xvfb
window. Gameplay inputs came from the separate probe callback. Raw images,
screenshots, input hashes and logs are under
`build/vita3k-readback-control/adventure-{double-buffer,external-host}/`, with
`game-comparison.json` joining the results. Both processes ended at the timeout
bound (exit 124), not through the game's normal shutdown sequence.

The full game still logs `Unhandled SIGSEGV` messages while continuing: 7,931 in
the double-buffer run and 2,033 in the external-host run. This is not a clean
emulator-log or general stability result. Fairy-camera activation, photograph
recognition/lifetime, other effects and physical Vita behavior remain open.
The hardware VPK `72c67b5` retains SHA-256
`b66668cfcd722e342ccb70af395b46f9084b608827c2a93e72a3a0c87d24b9f1`; neither the
physical Vita nor the macOS emulator profile was changed by these tests.

## Complete host photograph byte pipeline

`dk64_photo_pipeline_checks` extends the earlier crop/layout check through the
actual generated photograph (`806FFF88`), tile-copy (`806FFEAC`) and sepia
conversion (`806FFC04`) routines. It calls the native
`dk64_vita_sync_framebuffer` bridge and the shared GL sink's real readback path.
The bridge's renderer dispatch is synchronous in this fixture; runtime graphics
queue ordering is covered separately and is not re-tested here.

The test draws a 320x240 pattern on the GPU while guest RAM still contains older
data. Each photograph makes exactly one readback and one original 0xA000-byte
allocation. It checks every returned RGBA16 source pixel and every pixel of the
160x128 center crop, including the ten 32x64 tiles, channel packing, alpha,
sepia values, guest stack and allocation boundaries. Two source framebuffers use
KSEG0 and KSEG1 aliases; each is captured on its first use and after a changed
draw, with batching both disabled and enabled. All eight photographs match.
There is no preliminary read which could make a first-use failure disappear.

The original converter depends only on the sum of the three five-bit color
channels and the alpha bit. The US overlay constants at `8075DE80`/`8075DE88`
are 45.0 and 93.0, verified at decompressed ROM offsets `2162B80`/`2162B88`.
The independent integer reference is:

```text
s = red5 + green5 + blue5
red_out   = 3 + floor(28*s / 93)
green_out = 3 + floor(19*s / 93)
blue_out  = s < 48 ? 0 : floor(18*(s-48) / 45)
alpha_out = alpha_in
```

All 65,536 RGBA16 values agree with the generated converter in the game's
paired-register floating-point mode. This check runs for both batching cases,
giving 131,072 conversions plus 163,840 photograph output-pixel comparisons.
Even input `0000` becomes `18C0` (with alpha still zero), so nonzero photograph
storage alone would not establish a successful GPU capture.
The ASan/GLES test passed with Mesa softpipe and diagnostics disabled. Its local
log and input hashes are retained in `build/photo-pipeline-validation/`.

This establishes the complete pixel path on the host GL implementation. It does
not validate camera activation, fairy recognition, photograph actor lifetime,
Vita-side floating-point execution or hardware readback. In particular, it does
not change or resolve the first-read failure observed in Vita3K. No game,
renderer, emulator or physical-device executable was changed for this check.

## Repeating the diagnostic

Build `rt64_fast_smoke.vpk-vpk` using the normal readback options above and the
[Vita build instructions](VITA.md#renderer-validation-build). Run title `RT64F3001`
in an isolated Vita3K profile with the supplied shader compiler and installed
firmware. Keep Vulkan selected, including any per-title override. Save
`ux0/data/rt64-fast/` and the emulator log after each run before selecting the other
mapping mode.

The diagnostic writes `frame.ppm` for frame 2, `frame-3.ppm`, `frame-4.ppm`,
`frame-30.ppm`, and four `gpu-readback-N.rgba16` files. The latter are 8,192-byte,
top-to-bottom, big-endian 64x64 images. `progress.log` records exact current,
previous and zero pixel counts; a mismatch is recorded so the remaining cases can
still run. It must not be interpreted as passing because the program keeps running.

For an extracted Linux AppImage, set `APPIMAGE` to the original AppImage pathname
and `APPDIR` to its extracted root before invoking `AppRun`. This revision's
portable mode looks for built-in assets beside `usr/bin/Vita3K`, bypassing the
normal AppImage asset-path selection. In the isolated extraction, symlink the
bundled `usr/share/Vita3K` asset directories beside the executable. Put the profile
in `portable/` beside the AppImage; its default configuration must identify the
installed title before command-line title validation runs. These are test-profile
layout adjustments, not modifications to the emulator executable.

Local evidence for this run is under `build/vita3k-linux-control/`: the two
`*-alternating-result/` directories contain the raw images, logs and configurations;
`validation.json` records their hashes and independently counted pixel results.
The images, firmware and emulator binaries are excluded from Git. The physical
Vita's installed game, staged quiet VPK and game data were not modified by this
diagnostic work.
