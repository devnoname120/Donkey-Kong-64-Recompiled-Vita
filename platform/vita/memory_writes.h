#pragma once
#include "fast/rt64_fast.h"
#ifdef RECOMP_TRACK_MEMORY_WRITES
#include "librecomp/memory_writes.hpp"
#endif

inline void track_framebuffer_writes(RT64::FastDrawSink &sink) {
#ifdef RECOMP_TRACK_MEMORY_WRITES
    sink.setMemoryWriteTracking(recomp::watch_memory_writes);
#endif
}
inline void submit_framebuffer_writes(RT64::FastDrawSink &sink) {
#ifdef RECOMP_TRACK_MEMORY_WRITES
    const auto recorded=recomp::collect_memory_writes();
    if(recorded.empty()) return;
    std::vector<RT64::FastMemoryWrite> writes;
    writes.reserve(recorded.size());
    for(const auto &entry:recorded) writes.push_back({entry.address,entry.mask,entry.bytes});
    sink.notifyMemoryWrites(writes);
#endif
}
