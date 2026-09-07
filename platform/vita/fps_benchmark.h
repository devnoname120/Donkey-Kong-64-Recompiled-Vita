#pragma once
#include "fps_benchmark_logic.h"
#include "fast/rt64_fast_profile.h"

namespace VitaBenchmark {
enum class Kind : uint32_t { Game=1, Graphics, Present, Clocks, Thread, DepthRead, ColorRead };
void initialize();
int finishShutdown();
void poll();
void input(uint16_t *buttons,float *x,float *y);
void gameFrame(uint8_t *rdram);
void fail(const char *message) noexcept;
void milestone(const char *message);
uint64_t clockNow();
void completedSwap();
class Span {
    Kind kind;
    Scene scene{};
    Phase phase=Phase::Setup;
    uint64_t started=0;
    uint32_t sequence=0;
    bool sampled=false;
    RT64::FastProfile::Context context{clockNow};
    RT64::FastProfile::Context *previous=nullptr;
public:
    explicit Span(Kind value);
    ~Span();
    Span(const Span &)=delete;
    Span &operator=(const Span &)=delete;
};
}
