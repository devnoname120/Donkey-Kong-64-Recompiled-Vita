#pragma once
#include <cstdint>

struct ProbeGLStats {
    uint64_t uniformCalls=0, ignoredUniformCalls=0, repeatedUniformCalls=0, draws=0;
};
ProbeGLStats probeGLStats();
void resetProbeGLStats();
void reportProbeGLStats();
