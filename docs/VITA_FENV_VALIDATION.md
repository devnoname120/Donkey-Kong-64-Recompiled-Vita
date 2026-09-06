# Vita floating-point word conversion

The Vita recompiler helper uses ARM VFP `VCVTR` for `CVT.W.S` and `CVT.W.D`.
These instructions use the rounding mode in FPSCR. The non-Vita helpers retain
`lrintf` and `lrint`.

## Failure and isolation

A native photograph in Linux ARM64 Vita3K 4074-496939b6, Vulkan with
`external-host` mapping, exposed a conversion difference. The original photograph
routine captured a current, nonblack frame and produced all 20,480 sepia pixels.
Only 8,219 pixels matched the game's truncating sepia curve; 12,261 differed.
A round-to-nearest model matched all 20,480 pixels instead. Source pixels came
from the same captured image; the comparison accounts for the central 160x128
crop and the ten 32x64 tiles in the photograph allocation.

The original `806FFC04` routine temporarily selects truncation before its
floating-point-to-word conversions. ARM disassembly confirmed that `fesetround`
wrote FPSCR and the conversion helpers called the linked SDK `lrintf`/`lrint`.
The library implementation uses floating-point addition/subtraction of a large
constant. Its small-magnitude fast path also returns zero for values that need
rounding to +1 or -1 under a directed mode.

A separate native control application tested 12 positive/negative inputs under
all four guest rounding modes. It recorded FPSCR, `get_cop1_cs`, the actual
recompiler helpers, and direct VFP conversions. The original helpers differed
from the expected integers in 17 of 48 cases for each of the float and double
paths. Disabling Vita3K CPU optimizations did not change those results. Direct
`VCVTR` produced the expected integers in all 48 cases, with either CPU setting.
The revised helpers also passed all 48 cases with the normal CPU setting.

The rebuilt game then took a photograph in Jungle Japes through normal camera
inputs. All **20,480 pixels matched** the truncating sepia curve exactly, including
crop, tile layout, channel values and alpha. The picture actor was created and
displayed, its original deferred release completed, film decreased from ten to
nine, and the player returned to ordinary movement. This confirms the conversion
fix in the actual ARM game path; fairy recognition is a separate test.

This is native ARM execution through Vita3K. It does not establish that the
library's complete observed failure pattern occurs on physical hardware. The
source-level small-value limitation is separate from the emulator observations.
The control covers finite values within signed 32-bit range; it does not claim
MIPS exception emulation for NaN, infinities, or integer overflow.

## Reproduction

`platform/vita/fenv_smoke.c` is a separate application. Its expected integers are
explicit test data, independent of the implementation's conversion instructions.
It checks nearest-even ties, truncation, and both directed modes for positive and
negative values, including magnitudes below one half. It verifies FPSCR before
and after conversion as well as the guest-visible rounding mode.

Build it with the same VitaSDK container and source configuration as the game:

```sh
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build-cache \
  cmake --build build/vita --target DK64FenvControl.vpk-vpk -j4
```

The VPK has title ID `DK64FP001`. It writes
`ux0:data/dk64-fenv/results.csv` and exits with an error if a result or rounding
mode is wrong. This target adds no instrumentation or automatic input to the
normal game executable.

Local evidence is in `build/vita-fenv-control/{cpu-opt-on,cpu-opt-off,fixed}` and
`build/vita3k-camera-validation/z-lead`. The latter contains the original
photograph, source readback, pixel comparison, native screenshots and game logs.
The checked-in control's own VPK passed all 48 cases in
`build/vita-fenv-control/public`. The corrected game photograph and exact pixel
comparison are in `build/vita3k-camera-validation/japes-photo-first`.
ROM-derived image bytes and saves are local validation artifacts, not Git assets.

To repeat either photograph comparison with those local captures:

```sh
python3 tools/compare_vita_photograph.py \
  build/vita3k-camera-validation/japes-photo-first/readback-2-0004fa00.bin \
  build/vita3k-camera-validation/japes-photo-first/camera-photo-0.rgba16
```

The utility reports input hashes and exact mismatch counts; a mismatch returns
exit code 1. Applied to the earlier `z-lead` captures it reproduces all 12,261
mismatches. The current captures return zero mismatches and exit code 0.

## Camera integration probe

The optional `DK64_VITA_PROBE_CAMERA=ON` test requires diagnostics,
`DK64_VITA_SCRIPTED_INPUT=ON`, `DK64_VITA_PROBE_MAP=7`, and no pause script or
function profiler. It uses the separate `DK64RT001` application and
`ux0:data/dk64recompiled-probe` data/saves. Never distribute that VPK as the
normal game.

The probe grants camera ownership and ten film through the original game APIs,
enters Japes at its original exit 13, and supplies ordinary controller packets.
It observes the picture actor, writes its pixel allocation for comparison, and
forwards the original deferred release calls unchanged. It never writes fairy
completion flags, recognition results, actor positions or scores. At this
revision, the miss photograph and release are validated; the following scripted
approach to the pool fairy still encounters terrain, so successful recognition
and save persistence remain unproven.

`dk64_camera_probe_checks` covers prerequisite scope, caller/FPR preservation,
the required Z-before-C-Down input edge, shutter input, navigation basis, and
release forwarding. This fixture models game API callbacks and does not replace
native gameplay evidence.
