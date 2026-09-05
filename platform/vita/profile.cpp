#include "log.h"
#if DK64_VITA_PROFILE_FUNCTIONS
#include <atomic>
#include <cstdint>
#include <array>
#include <algorithm>
#include <unordered_map>
#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
extern "C" size_t ultramodern_debug_gfx_queue_size();
extern "C" void ultramodern_debug_queue_history(void (*)(const char *));
void dump_vita_queue_history() {
    ultramodern_debug_queue_history([](const char *line) { vita_log("%s",line); });
}
struct ProfileThread {
    std::atomic<unsigned> depth{0};
    std::atomic<int> id{0};
    std::array<std::atomic<uintptr_t>,32> stack{};
};
static std::array<ProfileThread,32> threads;
static std::atomic<unsigned> thread_count{0};
static thread_local ProfileThread *profile_thread=nullptr;
static thread_local unsigned profile_depth=0;
extern "C" void __cyg_profile_func_enter(void *function,void *) {
    if(!profile_thread) {
        unsigned index=thread_count.fetch_add(1);
        if(index>=threads.size()) return;
        profile_thread=&threads[index];
        profile_thread->id.store(sceKernelGetThreadId());
    }
    if(profile_depth<32) profile_thread->stack[profile_depth].store(reinterpret_cast<uintptr_t>(function),std::memory_order_relaxed);
    ++profile_depth;
    profile_thread->depth.store(profile_depth,std::memory_order_release);
}
extern "C" void __cyg_profile_func_exit(void *,void *) {
    if(profile_thread && profile_depth) profile_thread->depth.store(--profile_depth,std::memory_order_release);
}
void vita_log_guest_profile() {
    static uint64_t previous=0;
    static unsigned samples=0;
    const uint64_t now=sceKernelGetProcessTimeWide();
    if(now-previous >= (samples<12?1000000U:5000000U)) {
        if(samples==12) dump_vita_queue_history();
        vita_log("Runtime clock elapsed_us=%lld, process_us=%llu, queued graphics actions=%u",
            static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(ultramodern::time_since_start()).count()),
            static_cast<unsigned long long>(now),unsigned(ultramodern_debug_gfx_queue_size()));
        for(unsigned i=0;i<std::min<unsigned>(thread_count.load(),threads.size());++i) {
            auto &t=threads[i]; const unsigned depth=t.depth.load(std::memory_order_acquire);
            if(!depth) continue;
            const unsigned top=std::min(depth,32U)-1;
            vita_log("Guest stack thread 0x%x depth %u: ARM 0x%08x <- 0x%08x <- 0x%08x",
                t.id.load(),depth,unsigned(t.stack[top].load()),top?unsigned(t.stack[top-1].load()):0,
                top>1?unsigned(t.stack[top-2].load()):0);
        }
        ++samples;
        previous=now;
    }
}

extern "C" void __real_osSetTimer_recomp(uint8_t *,recomp_context *);
extern "C" void __real_osSetEventMesg_recomp(uint8_t *,recomp_context *);
extern "C" void __real_osRecvMesg_recomp(uint8_t *,recomp_context *);
extern "C" void __real_osSendMesg_recomp(uint8_t *,recomp_context *);
extern "C" void __real_osSpTaskStartGo_recomp(uint8_t *,recomp_context *);
static uint32_t sp_message=UINT32_MAX,dp_message=UINT32_MAX,timer_queue=0;
static std::unordered_map<uint32_t,unsigned> receive_traces,send_traces;
extern "C" void __wrap_osSetEventMesg_recomp(uint8_t *rdram,recomp_context *ctx) {
    vita_log("Register event %u queue 0x%08x message 0x%08x",unsigned(ctx->r4),unsigned(ctx->r5),unsigned(ctx->r6));
    if(ctx->r4==OS_EVENT_SP) sp_message=ctx->r6;
    if(ctx->r4==OS_EVENT_DP) dp_message=ctx->r6;
    __real_osSetEventMesg_recomp(rdram,ctx);
}
extern "C" void __wrap_osSetTimer_recomp(uint8_t *rdram,recomp_context *ctx) {
    static unsigned count=0;
    timer_queue=MEM_W(0x18,ctx->r29);
    if(count++<4) vita_log("SetTimer countdown=%llu queue=0x%08x",static_cast<unsigned long long>((ctx->r6<<32)|uint32_t(ctx->r7)),timer_queue);
    __real_osSetTimer_recomp(rdram,ctx);
}
extern "C" void __wrap_osRecvMesg_recomp(uint8_t *rdram,recomp_context *ctx) {
    const uint32_t queue=ctx->r4; const gpr output=ctx->r5;
    const bool trace=receive_traces[queue]++<3;
    if(trace) vita_log("Recv queue=0x%08x block=%u thread=%d",queue,unsigned(ctx->r6),sceKernelGetThreadId());
    __real_osRecvMesg_recomp(rdram,ctx);
    if(ctx->r2==0 && output) {
        const uint32_t msg=MEM_W(0,output);
        if(trace||msg==sp_message||msg==dp_message) vita_log("Received queue=0x%08x message=0x%08x",queue,msg);
    }
}
extern "C" void __wrap_osSendMesg_recomp(uint8_t *rdram,recomp_context *ctx) {
    const uint32_t queue=ctx->r4;
    if(send_traces[queue]++<3) vita_log("Send queue=0x%08x message=0x%08x block=%u",queue,unsigned(ctx->r5),unsigned(ctx->r6));
    __real_osSendMesg_recomp(rdram,ctx);
}
extern "C" void __wrap_osSpTaskStartGo_recomp(uint8_t *rdram,recomp_context *ctx) {
    vita_log("Start SP task at 0x%08x type=%u",unsigned(ctx->r4),unsigned(MEM_W(0,ctx->r4)));
    __real_osSpTaskStartGo_recomp(rdram,ctx);
}
#else
void vita_log_guest_profile() {}
#endif
