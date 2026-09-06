# Vita audio queue validation

The frontend now reserves one actual SDL device buffer when reporting queued
audio to DK64. This removes the recurring approximately 21 ms silent gaps in the
tested Vulkan Vita3K intro playback. Longer transient gaps and an intermittent
audio-production stop remain separate open issues. This is not physical-Vita
audio-quality or complete gameplay validation.

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
All 25 host checks, including the ASan capture checks, passed.

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
  VPK in `pulse-run4` produced the full 60-second sample capture. The cause is
  unresolved; the upstream wakeup fix alone is not proof of audio liveness under
  every schedule. The buffer reserve is not claimed to fix this intermittent stop.
- Audible fidelity, sound-menu behavior, gameplay continuity and physical-Vita
  latency/playback still require validation.

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
