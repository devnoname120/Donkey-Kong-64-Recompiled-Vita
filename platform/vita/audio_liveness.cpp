#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include <psp2/kernel/processmgr.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {
    enum Kind { SendBegin=1,SendEnd,ReceiveBegin,ReceiveEnd,BuildBegin,BuildEnd,SpSubmit };
    struct Event {
        uint64_t time;
        uint32_t kind,thread,queue,message;
        int32_t result,valid,capacity;
        uint32_t audio_state,audio_wait,detail;
    };
    std::array<Event,8192> events;
    std::mutex events_mutex;
    size_t event_count=0;
    bool frozen=false;
    std::atomic<uint64_t> last_pcm{0};
    bool watched(uint32_t queue) {
        return queue==0x80767a40 || queue==0x80767a98 || queue==0x8076d698 || queue==0x8076d6d0;
    }
    void record(uint8_t *rdram,Kind kind,uint32_t queue,uint32_t message,int32_t result,uint32_t detail=0) {
        const bool guest=ultramodern::is_game_thread();
        Event e{};e.time=sceKernelGetProcessTimeWide();e.kind=kind;e.queue=queue;e.message=message;e.result=result;e.detail=detail;
        e.valid=e.capacity=-1;
        // Queue and OSThread state are sampled only on a serialized guest thread.
        if(guest) {
            e.thread=TO_PTR(OSThread,ultramodern::this_thread())->id;
            const auto *audio=TO_PTR(OSThread,int32_t(0x8076d4e8));
            e.audio_state=audio->state;e.audio_wait=audio->queue;
            if(watched(queue)) { const auto *mq=TO_PTR(OSMesgQueue,int32_t(queue));e.valid=mq->validCount;e.capacity=mq->msgCount; }
        }
        std::lock_guard lock(events_mutex);
        if(!frozen) events[event_count++%events.size()]=e;
    }
}

void vita_audio_liveness_progress() { last_pcm.store(sceKernelGetProcessTimeWide(),std::memory_order_relaxed); }
void vita_audio_liveness_poll(const char *directory) {
    const uint64_t now=sceKernelGetProcessTimeWide(),last=last_pcm.load(std::memory_order_relaxed);
    if(!last || now<last || now-last<2000000) return;
    std::vector<Event> copy;
    {
        std::lock_guard lock(events_mutex);
        if(frozen) return;
        frozen=true;
        const size_t first=event_count>events.size()?event_count-events.size():0;
        copy.reserve(event_count-first);
        for(size_t i=first;i<event_count;++i)copy.push_back(events[i%events.size()]);
    }
    FILE *out=std::fopen((std::string(directory)+"/audio-liveness.csv").c_str(),"w");
    if(!out) return;
    std::fprintf(out,"last_pcm_us,%llu\nobserved_us,%llu\ntime_us,kind,thread,queue,message,result,valid,capacity,audio_state,audio_wait,detail\n",
        static_cast<unsigned long long>(last),static_cast<unsigned long long>(now));
    for(const auto &e:copy)std::fprintf(out,"%llu,%u,%u,%08x,%08x,%d,%d,%d,%u,%08x,%08x\n",
        static_cast<unsigned long long>(e.time),e.kind,e.thread,e.queue,e.message,e.result,e.valid,e.capacity,e.audio_state,e.audio_wait,e.detail);
    std::fclose(out);
}

extern "C" s32 __real_osSendMesg(uint8_t *,PTR(OSMesgQueue),OSMesg,s32);
extern "C" s32 __wrap_osSendMesg(uint8_t *rdram,PTR(OSMesgQueue) queue,OSMesg message,s32 flags) {
    if(watched(queue))record(rdram,SendBegin,queue,message,flags);
    const s32 result=__real_osSendMesg(rdram,queue,message,flags);
    if(watched(queue))record(rdram,SendEnd,queue,message,result);
    return result;
}
extern "C" s32 __real_osRecvMesg(uint8_t *,PTR(OSMesgQueue),PTR(OSMesg),s32);
extern "C" s32 __wrap_osRecvMesg(uint8_t *rdram,PTR(OSMesgQueue) queue,PTR(OSMesg) message,s32 flags) {
    if(watched(queue))record(rdram,ReceiveBegin,queue,message,flags);
    const s32 result=__real_osRecvMesg(rdram,queue,message,flags);
    if(watched(queue))record(rdram,ReceiveEnd,queue,result==0&&message?MEM_W(0,message):0,result);
    return result;
}
extern "C" void __real_func_global_asm_80601EE4(uint8_t *,recomp_context *);
extern "C" void __wrap_func_global_asm_80601EE4(uint8_t *rdram,recomp_context *ctx) {
    const uint32_t info=ctx->r4;
    record(rdram,BuildBegin,0,info,0,uint32_t(ctx->r5));
    __real_func_global_asm_80601EE4(rdram,ctx);
    const int32_t synth=MEM_W(0,0xffffffff807563e4ULL);
    record(rdram,BuildEnd,0,info,int32_t(ctx->r2),synth?MEM_W(0,synth):0);
}
extern "C" void __real_osSpTaskStartGo_recomp(uint8_t *,recomp_context *);
extern "C" void __wrap_osSpTaskStartGo_recomp(uint8_t *rdram,recomp_context *ctx) {
    if(MEM_W(0,ctx->r4)==M_AUDTASK)record(rdram,SpSubmit,0,uint32_t(ctx->r4),0);
    __real_osSpTaskStartGo_recomp(rdram,ctx);
}
