# Physical benchmark shutdown crash

## Finding

The benchmark's planned exits were not crash tests. A real application shutdown
race occurred after the timing files had been written. The runner incorrectly
accepted the capture and executable-restoration receipt without checking whether
process teardown had faulted.

Twenty new `psp2core-*.psp2dmp` files were retrieved from `ux0:data` on September 7,
2026. They span 20:15:34 through 22:38:23 CEST. Every dump identifies `DK64RE001`,
the same faulting GXM instruction, and the process-exit thread releasing GPU-driver
resources. They cover reference builds as well as intermediate and final FPS
candidates. This is not evidence that the final byte-range optimization introduced
a new in-game memory error.

All originals remain on the device. Compressed copies, remote file sizes and
SHA-256 hashes are preserved locally in `build/crash-investigation-20260907/`.
`download-manifest.json` identifies each copied file. `dump-summary.json` records
module-relative PCs and the timestamp-correlated benchmark run. Historical run
association uses the saved deployment chronology; the most recent package was
also checked directly against the preserved build and its deployment manifest.
Raw dumps contain process memory and are not release assets.

## Latest dump and binary identity

The latest dump is
`psp2core-1788813503-0x0000c123d1-eboot.bin.psp2dmp`:

- Crash time: September 7, 2026, 22:38:23 CEST.
- Compressed size: 640,992 bytes.
- Compressed SHA-256:
  `c3e3b588b0778421e7d75e5f08977af58dbeb802292a893d69b725b529729333`.
- Correlated run: `build/fps-autobench/range-rap-02/`.
- Benchmark executable SHA-256:
  `d780fa4f9a9aee72f71fd3e36e400cee5a1f87b29f0003adb0f68238502cf9ac`.
- Package SHA-256:
  `098cb8f4ccb57582dee6d9a4bdc81c5a31a6d9b379823a1d3349531d2afb4af0`.

The executable extracted from `packages/ranges-contained.vpk` and the existing
benchmark build had that same hash. Its ELF, relocated VELF and VPK were preserved
as `faulting-benchmark.*` before rebuilding. The run's capture contains 17,174
records and an approximately 120-second measured interval. Its completion record
was written before process teardown; the subsequent coredump is not a successful
exit receipt.

## Fault analysis

ELF core thread-register records and loaded-module segments were decoded locally.
Firmware code was inspected with IDA MCP using the local 3.60 modules, without
internet sources. Segment relocation was applied separately for text and data;
the IDB's lowest image address is not necessarily the text-segment base.

| Item | Observed value in the latest dump |
| --- | --- |
| Faulting thread | `pthread`, UID `0x40010175` |
| Fault PC | `SceGxm + 0xf074`, runtime address `0xe009df84` |
| Fault LR | `SceGxm + 0x90d4` |
| Fault instruction | `LDR R3, [R1, #4]` |
| R1 at fault | `0x81a60270` |
| Main thread PC | `SceGpuEs4User + 0x1758` |
| Main thread LR | Return into `SceGpuEs4User`'s `module_exit` after `PVRSRVRelease` |

In the reference GXM image the fault maps to `0x8116fec4`, inside an address-range
validation helper. It dereferences metadata belonging to the GPU driver's memory
management. The main thread's return address maps to `module_exit` at
`0x810d8d30` in the reference `SceGpuEs4User` image. IDA shows that this exit path
has already destroyed its allocation space and freed its memory block before
calling the release service.

Thus GPU-driver teardown was concurrent with a still-running graphics worker.
The register value `0x805b0003` is the helper's preloaded invalid-value return
code, not proof that an ordinary returned GXM error caused the exception.
The instruction actually faulted on a memory read.

All 20 dumps have the same module-relative fault PC and LR and the same main-thread
teardown location. This uniform signature, together with the source's explicit
exit call and the corrected hardware runs, identifies the benchmark lifecycle as
the cause. It does not explain unrelated historical dumps, transport timeouts, or
all possible gameplay failures.

## Correction

`VitaBenchmark::exportData()` no longer calls `sceKernelExitProcess()` directly.
After closing the capture files it requests `ultramodern::quit()` and returns to
the runtime's existing main loop. The runtime finishes the current renderer call,
shuts the renderer down on its owning thread, joins its graphics/VI/RSP event
workers and returns to the frontend. Renderer destruction retains `glFinish()`;
no GPU lifetime or synchronization shortcut was introduced.

After the runtime returns and SDL audio is closed, the frontend writes a separate
`RUN-shutdown.json` receipt and returns normally. The receipt is not written at
capture completion or before the graphics-worker join. This change is confined
to the benchmark path; it does not alter normal gameplay, the rendering algorithm,
FPS settings, or the audio scheduling fix.

The runner now treats these as independent checks:

1. The finite scenario produced a complete, accepted performance capture.
2. The runtime returned through the ordered shutdown path.
3. No new or changed coredumps appeared during the session, including cleanup.
4. The original executable was restored and normal saves were unchanged.

New or changed dumps are copied locally and left untouched on the Vita. A failed
inventory or missing shutdown receipt cannot yield a clean result. Analysis and
directory comparison reject known-crashed captures even when their timing JSON
says `complete: true`. Historical captures without lifecycle evidence are marked
`clean_shutdown_verified: false` rather than silently certified.

Regression tests first reproduced the original false acceptance of a completed
capture followed by a crash and of a missing shutdown. Additional tests cover
copied dump preservation, stale run identity, inspection failures and comparison
rejection. Existing executable-recovery safeguards remain in effect.

## Verification scope

Physical validation results are recorded separately in
`build/crash-investigation-20260907/crashfix-*/`, including capture data, shutdown
receipt, before/after coredump inventories, executable-restoration proof and save
hashes. These are repeated runs on the actual Vita, not Vita3K exit behavior.
The final verification summary lists the completed cases and test counts.

The September 7 final device check at 23:56:57 CEST verified five clean runs:
`crashfix-rap-01`, `crashfix-rap-02`, `crashfix-rap-03`, `crashfix-world-01`, and
`crashfix-world-02`. All had ordered shutdown receipts, no new dumps, preserved
normal saves and a restored normal executable. The second world run required
guarded executable recovery after host supervision was interrupted; its application
had already completed orderly shutdown. The summary is
`build/crash-investigation-20260907/final-crash-verification.json`. The host suite
passed 38 checks (including the unrelated local audio-timing check); the runner
and comparison tests passed 18 and 8 checks respectively.

Earlier timing files are retained as pre-exit measurements. They must not be
relabelled as clean stability tests. The performance release should use fresh
lifecycle-validated cases, and this shutdown correction does not by itself certify
a complete playthrough or solve residual audio stutter.
