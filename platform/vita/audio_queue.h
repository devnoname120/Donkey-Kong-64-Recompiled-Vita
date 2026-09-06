#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

inline size_t vita_audio_frames_remaining(size_t queued_frames,uint32_t rate,size_t device_frames) {
    // SDL drains a complete device buffer at once. Keep at least that much in
    // reserve when DK64 decides how much audio to generate for its next task.
    // Also retain the desktop frontend's one-VI reserve for smaller devices.
    const size_t reserve=std::max(device_frames,size_t(rate/60));
    return queued_frames>reserve ? queued_frames-reserve : 0;
}
