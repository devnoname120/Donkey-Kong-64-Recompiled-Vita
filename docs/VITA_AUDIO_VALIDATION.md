# Vita audio queue validation

The frontend now reserves one actual SDL device buffer when reporting queued
audio to DK64. This removes the recurring approximately 21 ms silent gaps in the
tested Vulkan Vita3K intro playback. An idle-receiver guard also fixes a captured
audio-production deadlock during catch-up VIs. Longer transient gaps and broader
audio stability remain open. This is not physical-Vita audio-quality or complete
gameplay validation.

## Cause and change

The native port opens S16 stereo at the game's 22,050 Hz rate, with a 1,024-frame
SDL buffer. The frontend previously returned the raw SDL queue depth to
`ultramodern::get_remaining_audio_bytes()`. SDL drains a complete device buffer
at once; the original game's `func_global_asm_80601EE4` uses the reported depth
to choose 552, 736 or 920 frames for a later audio task.

Capturing both sides of SDL showed underfilled output buffers during otherwise
regular production. For example, at process time 20,752,320 us the game enqueued
552 frames. SDL submitted its next 1,024-frame buffer at 20,785,602 us, before
the next 736-frame enqueue at 20,785,827 us. Its trailing 472 frames were zero:
21.406 ms of padding. Every non-silent device-buffer segment in that capture
matched the producer PCM exactly, in order (1,241 of 1,241 segments).

This matches SDL 2.32.8's
[queue-drain implementation](https://github.com/libsdl-org/SDL/blob/release-2.32.8/src/audio/SDL_audio.c),
which pads an exhausted queue with silence. Its
[Vita backend](https://github.com/libsdl-org/SDL/blob/release-2.32.8/src/audio/vita/SDL_vitaaudio.c)
submits that buffer through `sceAudioOutOutput`. The captured Vita port was BGM,
22,050 Hz, stereo, 1,024 frames; there was no Vita-side sample-rate conversion.

`platform/vita/audio_queue.h` now reports
`max(queued_frames - max(device_frames, rate / 60), 0)`. The one-VI minimum
retains the desktop frontend's buffering policy for smaller device buffers;
the actual SDL buffer size covers the coarser Vita drain interval. The runtime's
existing additional adjustment remains in place. Sample rate, PCM, task timing
and the 1,024-frame device configuration are unchanged. The extra reserve can
increase audio latency by roughly one device period (46.4 ms here); this tradeoff
still needs evaluation on hardware.

## Evidence

Tests used the official Vita3K 4074-496939b6 ARM64 Linux artifact with Vulkan,
CPU optimizations enabled, double-buffer memory mapping and surface sync. SDL
audio played into a private PulseAudio null sink configured as 48,000 Hz S16LE
stereo. `parec` recorded its monitor. The stream metadata confirmed one Vita3K
sink input at that format/rate, unmuted at 100 percent, without Pulse resampling.
No controller input was injected. Shader caches were warm.

The table counts exact-zero gaps of at least 10 ms in capture seconds 15–50.
This interval excludes startup and the later transient around 51 seconds; those
are reported below rather than counted as fixed.

| Checkpoint | Before reserve | With reserve |
| --- | ---: | ---: |
| Probe: concatenated generated PCM | 0 | 0 |
| Probe: buffers submitted to `sceAudioOutOutput` | 27 | 0 |
| Probe: PulseAudio output | 27 | 0 |
| Quiet release: PulseAudio output | 25 | 0 |

The fixed probe's 1,242 non-silent device-buffer segments all matched producer
PCM exactly. Both producer captures reached 60 seconds of samples. The quiet
release completed the 70-second run, created no game diagnostic logs and left
both save-file hashes unchanged. Local evidence is under the ignored
`build/vita3k-linux-control/audio-validation/` directory:

- `pulse-run1`: quiet baseline at source revision `5330963`.
- `pulse-run5`: baseline producer/device/queue/output capture.
- `pulse-run6-reserve`: corresponding capture with the reserve.
- `pulse-run7-quiet`: quiet release with the reserve; VPK SHA-256
  `ab701813a932da5c5dc60d93af112cc0666589c6048c75d02176c839f1f9cec5`.

Each run retains its input hashes, emulator log, Pulse metadata and PCM; the
probe runs also retain chunk indexes and exact producer-to-device mappings.
Game PCM and ROM data are local validation inputs, not repository assets.
An earlier SDL disk-driver experiment is explicitly invalid for quality
assessment because its actual format and pacing differed from the requested
settings; it is not used in these results.

`dk64_audio_queue_checks` runs the actual generated `func_global_asm_80601EE4`
and the runtime's AI-length conversion against block-draining devices. It covers
35 initial phases at each of 256, 512 and 1,024 frames, with and without the
reserve: 210 runs of 30 seconds. After a two-second startup allowance, the
original 1,024-frame configuration accumulates 504,088 padding frames across its
35 runs. All reserve-enabled runs have zero padding, with a maximum queue of
2,760 frames. Synthesis and operating-system scheduling are outside this fixture.
All 26 host checks, including the ASan capture and wakeup checks, passed.

The quiet ELF was checked for disabled diagnostics, scripted input, capture,
profiling and renderer tracing; capture/wrapper symbols, diagnostic strings and
debug sections were absent. The requested RT64/vitaGL readback speedhack remains
enabled. CMake rejects audio capture in quiet or scripted-input configurations.

## Remaining failures

- Around capture second 51, the fixed diagnostic run still contains a 29.0 ms
  device-buffer gap. The quiet run has two gaps of 20.94 and 12.56 ms in that
  region. The earlier quiet baseline had a 99.65 ms transient plus its regular
  underruns. These captures do not establish the cause of the remaining stall.
- Startup and the logo-to-intro change can still run short of audio, including
  a roughly 459 ms gap in the fixed diagnostic capture.
- In `pulse-run3`, an earlier baseline capture stopped producing new game PCM
  after process time 14.651 seconds while graphics continued. Repeating the same
  VPK in `pulse-run4` produced the full 60-second sample capture. That run did not
  capture scheduler events, so its cause cannot be assigned retrospectively.
  The separately traced deadlock below is fixed; longer-run liveness remains a
  validation requirement.
- Audible fidelity, sound-menu behavior, gameplay continuity and physical-Vita
  latency/playback still require validation.

## Catch-up VI deadlock

An instrumented 90-second run with the same Vulkan profile and Docker limited
to 1.5 CPUs reproduced a permanent production stop. This is a scheduling stress
test, not a hardware performance measurement. The last PCM submission was at
process time 47,330,293 us, while graphics continued through the rest of the run.
The event ring captured no failed queue sends or empty audio builds.

The decisive event sequence in `liveness-run2-limited-cpu/audio-liveness.csv` is:

1. At 47,318,469 us, the audio thread consumes notification 5 and requests that
   the scheduler start the previously queued audio task (`0x29E`).
2. Before servicing that request, the scheduler handles queued VIs. At
   47,320,220 us it sends another notification 5 because the notification queue
   is empty, although the audio thread is already processing the first one.
3. The previous task starts. The audio thread builds and queues another task,
   waits for completion, then consumes the extra notification immediately.
4. The scheduler has not yet drained the new task queue, so the audio thread
   sees no pending task and sends no start request. It builds a second task and
   blocks on completion queue `0x8076D6D0`.
5. At 47,350,571–576 us the scheduler accepts both queued tasks, but neither
   starts: the thread which must request a start is waiting for completion.
   A further notification sits unread in queue `0x8076D698`.

`dk64_vita_notify_audio` now requires both an empty notification queue and a
receiver blocked on that queue. The runtime maintains `blocked_on_recv` at
offset zero and pops the receiver as soon as it delivers a message. Guest
execution is serialized, so a consumed notification cannot be replaced while
the audio thread is running or waiting for task completion. The original startup
notification remains unchanged.

`dk64_audio_wakeup_checks` executes the actual generated audio loop and native
notification hook with the captured interleaving. The old empty-only guard
stalls after three builds and one completion, leaving two unstarted tasks. The
idle-receiver guard completes 120 tasks from 121 builds without stalling. The
fixture models scheduler queues and completion; it does not run RSP synthesis.

The fixed native repeat, `liveness-run3-guard-limited-cpu`, ran for 90 seconds
under the same CPU limit without triggering the two-second submission-gap
detector. Its bounded producer capture reached process time 71,406,423 us and
1,267,208 stereo frames before its wall-time limit. This proves progress past
the captured failure point, not full-speed playback under that CPU limit.
The recorded output remains non-silent through its end at 88.034 seconds;
the failing baseline's output is entirely zero after 46.274 seconds. Under
the CPU limit, the fixed run still has frequent short gaps (185 gaps of at least
10 ms in capture seconds 15–50). Preserving liveness does not make that constrained
run meet the audio delivery deadlines.

The unthrottled quiet follow-up, `liveness-run4-quiet`, also ran for 90 seconds.
It has nonzero output through capture end at 88.042 seconds and no gaps of at
least 10 ms in seconds 15–50. A 57.19 ms gap remains near second 50.80. Save hashes
are unchanged and no game logs or capture files were created. Its VPK SHA-256 is
`b66668cfcd722e342ccb70af395b46f9084b608827c2a93e72a3a0c87d24b9f1`.
Both quiet builds retain the previously measured buffering fix. The different
transient durations are observations, not evidence that all load-related gaps
were improved.

The event recorder exists only in the separate audio probe. It keeps the last
8,192 events in memory and exports `audio-liveness.csv` after observing two
seconds without a successful PCM enqueue. It observes sends, receives, audio
build results and SP submissions; queue/thread fields are sampled on serialized
guest threads. It neither schedules tasks nor injects controller input. Function
profiling is rejected for this probe because both modes intercept SP submission.

## Separate capture build

`DK64_VITA_AUDIO_CAPTURE=ON` creates **DK64 Audio Probe**, title `DK64AU001`, with
normal controls and its own `ux0:data/dk64recompiled-audio` data directory. Put your
`DK64.z64` there and use separate probe saves. Configure with
diagnostics on, scripted input off and profiling/tracing off. For example, after
generating the game sources and building the SDK image described in VITA.md:

```sh
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build-quiet \
    cmake -S . -B build/vita-audio -DDK64_VITA=ON \
    -DDK64_VITA_DIAGNOSTICS=ON -DDK64_VITA_AUDIO_CAPTURE=ON \
    -DDK64_VITA_SCRIPTED_INPUT=OFF -DDK64_VITA_SCRIPTED_PAUSE=OFF \
    -DDK64_VITA_PROFILE_FUNCTIONS=OFF -DDK64_VITA_TRACE_RENDERER=OFF \
    -DCMAKE_BUILD_TYPE=Release
docker run --rm --platform linux/amd64 -v "$PWD:/project" dk64-vita-build-quiet \
    cmake --build build/vita-audio --target DK64AudioProbe.vpk-vpk -j6
```

The probe copies PCM into bounded memory on the producer/device threads and
exports from the main thread after each capture finishes. Each capture stops at
60 sample-seconds, a sample-rate change, 8,192 chunks or the first main-thread
check after 70 wall-seconds. PCM writes never run while holding either audio
mutex. Exports can stall other work, so compare intervals before the first export.

- `audio-capture.s16le` and `.csv`: stereo PCM just before `SDL_QueueAudio`,
  after the RDRAM channel swap, plus rate, stop reason and timestamp/frame index.
- `device-audio-capture.s16le` and `.csv`: corresponding PCM submitted to the
  actual stereo BGM port. The initial dummy 48 kHz main port is excluded.
- `audio-capture-queue.csv`: bounded observations of raw SDL queue depth in
  stereo frames, before the frontend reserve and runtime adjustment.

The probe records submitted data, not successful physical playback or device
return timing. `DK64_VITA_AUDIO_CAPTURE` defaults to OFF and compiles out of the
normal game. Use the quiet package for hardware testing.
