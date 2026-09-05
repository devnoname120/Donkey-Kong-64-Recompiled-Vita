#pragma once
#include "fast/rt64_fast.h"
#include <memory>
#include <string>

struct ProbeEGL {
    virtual ~ProbeEGL() = default;
    virtual std::unique_ptr<RT64::FastDrawSink> createSink() = 0;
};
std::unique_ptr<ProbeEGL> createProbeEGL(const std::string &directory);
