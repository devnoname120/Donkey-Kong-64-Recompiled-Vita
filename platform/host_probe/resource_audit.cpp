// Host-only observations of the original deferred asset-loading path.
#include "recomp.h"
#include <atomic>
#include <cstdint>
extern void vita_log(const char *,...);
extern "C" void __real_getPointerTableFile(uint8_t *,recomp_context *);
extern "C" void __real_func_global_asm_8066AF40(uint8_t *,recomp_context *);
namespace {
    std::atomic<unsigned> loads{0},queued{0},drains{0},peak_jobs{0},peak_messages{0};
    void maximum(std::atomic<unsigned> &value,unsigned next) {
        unsigned old=value.load(std::memory_order_relaxed);
        while(old<next && !value.compare_exchange_weak(old,next,std::memory_order_relaxed)) {}
    }
    unsigned observe(uint8_t *rdram) {
        const unsigned count=uint32_t(MEM_W(0,0xffffffff807f9680ULL));
        maximum(peak_jobs,count);
        if(MEM_W(16,0xffffffff807656d0ULL)==192)
            maximum(peak_messages,uint32_t(MEM_W(8,0xffffffff807656d0ULL)));
        return count;
    }
}
extern "C" void __wrap_getPointerTableFile(uint8_t *rdram,recomp_context *ctx) {
    ++loads; const auto before=observe(rdram);
    __real_getPointerTableFile(rdram,ctx);
    if(observe(rdram)>before) ++queued;
}
extern "C" void __wrap_func_global_asm_8066AF40(uint8_t *rdram,recomp_context *ctx) {
    ++drains; observe(rdram);
    __real_func_global_asm_8066AF40(rdram,ctx);
    observe(rdram);
}
void report_resource_audit() {
    vita_log("PROBE resources: loads=%u queued_load_calls=%u drains=%u peak_jobs=%u peak_completions=%u original_capacity=192",
        loads.load(),queued.load(),drains.load(),peak_jobs.load(),peak_messages.load());
}
