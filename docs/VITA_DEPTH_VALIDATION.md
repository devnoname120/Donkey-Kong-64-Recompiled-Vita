# Depth readback and DK64 visibility queries

## Contract

`FastDrawSink::readDepthFramebuffer(address, size, bytes)` returns the requested
byte range from a resident N64 depth image. The result is big-endian, compressed
16-bit N64 Z, not an RGBA reinterpretation of the depth attachment. An unknown
image returns `false` without changing the caller's destination. Zero-byte reads
succeed with an empty result. Reads crossing an image boundary or overflowing
the 32-bit address space fail.

Depth images retain their existing address/width/height identity. Color targets
can share a depth image; a color-image switch does not clear it. A depth write or
clear invalidates that image's CPU readback cache. Reads flush a batching sink
before observing the underlying image. Repeated queries of unchanged depth reuse
the resolved image; only the requested pixels are packed into N64 bytes.

The conversion rounds the sampled depth to an 18-bit integer, then applies the
exponent/mantissa representation used by RT64's `Depth.hlsli`. The two visible
delta-Z bits are zero. This path does **not** reconstruct RDP delta-Z or hidden
coverage state; DK64's visibility decoder does not read those bits.

## GPU implementation

The GLES validation sink uses `GL_OES_depth_texture`. The Vita sink exposes its
existing persistent depth attachment through `vglTexImageDepthBuffer` and samples
the F32M red component. The other components of that one-component format are not
inputs to the conversion. A vec4 dot-product selection produced incorrect results
on the tested Vita compiler path even when the extra component weights were zero.

Vita depth rows are padded to 32 samples. For widths not divisible by 32, the
sampled view exposes that physical row width and scales the horizontal UV range
to exclude the padding. The generated shader writes the 18-bit result to RGB
bytes; a completed `vglReadPixels` GPU transfer into reusable, aligned memory
then supplies the CPU packing step. This avoids directly reading the render
attachment while it is GPU-owned. Depth queries explicitly finish the relevant
drawing even when optional delayed color readbacks are enabled. The transfer
implementation also waits for transfer completion before exposing its output.

The CPU cache retains the resolved RGB depth bytes in their native row order.
Packing and row lookup are restricted to the requested byte range, including odd
starts and ends. A one-pixel visibility query therefore no longer repacks all
76,800 pixels of a 320-by-240 image. The cache uses four bytes per pixel instead
of the earlier two-byte packed cache; the GPU resolve and transfer still cover
the whole image when its write version changes. This is not asynchronous depth
readback and does not remove GPU synchronization costs.

The odd-size regression exposed an independent vitaGL issue: the pinned
`glViewport` implementation used integer shifts to calculate its half-width and
half-height. A 37-by-19 viewport therefore rasterized only 36-by-18 pixels. The
Docker build applies `platform/vita/patches/vitagl-viewport.patch`, changing those
two calculations to floating-point multiplication by 0.5. The dependency pin
otherwise remains unchanged.

## DK64 integration

The original `func_global_asm_80700AE4` remains recompiled. Its entry hook makes
only the queried GPU Z pixel visible in RDRAM before the original viewport test,
compressed-Z decoder, and callers' world-distance conversion run. The bridge
uses the original depth base, screen width, and viewport. It does not manufacture
a successful visibility result or change fairy flags.

Requests travel through the same ordered graphics submission path as color
readbacks. KSEG0 and KSEG1 aliases resolve to the same resident image. A missing
GPU image preserves the existing RAM value. A malformed response is rejected
before modifying RAM. Neither neighboring bytes nor the caller's recompiler
context are modified.

## Regression coverage

The common depth checks are used by `rt64_depth_checks` on GLES and by the native
`RT64DepthControl.vpk` (`RT64Z0001`). They cover a first read after drawing without
a preliminary color/depth read, far clears, nearer and occluded geometry,
write-disabled draws, partial clears, shared and independent depth images,
odd-byte subranges, invalid ranges, and batched submissions.

`dk64_depth_query_checks` executes the retained generated DK64 routine through
the production bridge and a real GLES sink. It checks decoded distances, first
GPU reads, changed and unchanged pixels, both cached/uncached memory aliases,
both recompiler FPR layouts, viewport rejection, RAM fallback, short-response
rejection, and caller-register preservation.

On 2026-09-07, the native control passed all **54 exact images** for 32-by-24,
37-by-19 and 320-by-240 targets, with batching both off and on. It also passed
100 alternating near/far redraw-and-query iterations and another 100 with
presentation between iterations. No framebuffer read primed the first-use
checks. The recorded normal build is
`build/vita3k-depth-validation/native-17-gpu-transfer/`.

A fresh dependency-image build with shader/file logging disabled and
`READBACKS_SPEEDHACK=1` repeated the complete 54-image and 200-iteration checks in
`build/vita3k-depth-validation/native-19-quiet-external/`. The exact Vita3K
`496939b6` Linux AArch64 build used Vulkan/Lavapipe, external-host mapping and
CPU optimization enabled. The matched quiet binary with mapping disabled failed
its first depth read: it returned zero instead of packed depth `0x2000`.
`native-18-quiet-disabled/` preserves that negative control. GPU transfers do not
remove this emulator mapping requirement.

The successful controls completed and wrote `PASS`; the emulator subsequently
aborted in its in-process app-close/relaunch path. These pixel checks are not
evidence of a clean emulator shutdown or physical-Vita behavior. The complete
30-test host suite, including the retained DK64 query, passed separately in
`build/continuation-host-suite.log`.

The extended precision control passed **60 exact images** and both 100-iteration
stress variants in `build/vita3k-depth-validation/native-20-precision-external/`.
The additional cases exercise exponent/mantissa boundaries and non-dyadic depth
values, rather than only half/quarter-depth planes.

The range-packing refinement repeated all 60 images and 200 stress iterations in
`build/vita3k-depth-validation/timing-after-lazy-pack/`, including a first-use
odd-byte query spanning a row boundary before the full-image check. The matching
`timing-before-lazy-pack/` control took 125,497 microseconds for 100 redraw/query
iterations; the refined implementation took 107,626 microseconds. Including
presentation, the corresponding totals were 360,888 and 354,980 microseconds.
These are single sequential emulator runs, not hardware performance estimates or
a statistically controlled benchmark. Both used the quiet dependency build,
external-host mapping and CPU optimization enabled.

Final host validation passed all 31 tests in
`build/final-vita-validation/lazy-depth-host-suite.log`. A review also found that
the optional tracing wrapper omitted depth-query forwarding. Its new regression
fails with that omission and passes with the forwarding restored, including
missing-image and exception behavior. Texture traces in the scripted probe now
stay in the probe's own data directory. A complete traced native camera run,
`build/vita3k-camera-validation/review-traced-capture/`, passed both exact
photographs and post-reward completion before the range-packing refinement.

The separate [camera and save/restart validation](VITA_CAMERA_VALIDATION.md) now
establishes successful fairy recognition, exact miss/success photograph pixels,
both deferred frees, return to movement and permanent-flag persistence in a fresh
process. Its gameplay configuration disables CPU optimization; the passing depth
controls with optimization enabled do not establish full-game emulator stability.
