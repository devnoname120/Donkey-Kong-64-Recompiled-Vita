// Replay the observed VI/consumer interleaving through the real generated
// audio loop. The fixture models scheduler queues and task completion, not RSP
// synthesis or host thread timing.
#include "recomp.h"
#include "ultramodern/ultra64.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_80601D24(uint8_t *,recomp_context *);
extern "C" void dk64_vita_notify_audio(uint8_t *,uint32_t);
static constexpr gpr wake=0xffffffff8076d698ULL,done=0xffffffff8076d6d0ULL;
static constexpr gpr scheduler=0xffffffff80767a40ULL,audio_thread=0xffffffff8076d4e8ULL;
static unsigned commands,pending,active,built,completed,extra_wakes;
static bool guarded;
struct Finished {};
struct Stalled {};
static void check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
extern "C" s32 __wrap_osSendMesg(uint8_t *rdram,PTR(OSMesgQueue) queue,OSMesg message,s32 flags) {
    check(uint32_t(queue)==uint32_t(wake)&&message==5&&flags==OS_MESG_NOBLOCK,"unexpected direct notification");
    check(MEM_W(8,wake)<8,"wakeup queue overflow");
    if(!MEM_W(0,wake))++extra_wakes;
    ++MEM_W(8,wake);MEM_W(0,wake)=0;
    return 0;
}
static void notify(uint8_t *rdram) {
    if(guarded)dk64_vita_notify_audio(rdram,uint32_t(wake));
    else if(MEM_W(8,wake)==0)__wrap_osSendMesg(rdram,int32_t(wake),5,OS_MESG_NOBLOCK);
}
static void drain_commands(uint8_t *rdram) {
    pending+=commands;commands=0;MEM_W(0x264,scheduler)=pending?int32_t(0x80210000):0;
}
extern "C" void func_global_asm_8060ED6C(uint8_t *,recomp_context *) {}
extern "C" void osSendMesg_recomp(uint8_t *rdram,recomp_context *ctx) {
    if(uint32_t(ctx->r4)==uint32_t(wake)) {
        ctx->r2=__wrap_osSendMesg(rdram,int32_t(wake),ctx->r5,ctx->r6); // Startup message.
    } else {
        check(ctx->r4==scheduler&&ctx->r5==0x29e&&ctx->r6==OS_MESG_BLOCK,"unexpected scheduler request");
        check(pending&&!active,"audio requested an unavailable task");
        // The audio thread has consumed its wakeup and yielded to the scheduler.
        // The native failure had queued VIs here before the task-start request.
        notify(rdram);notify(rdram);
        --pending;++active;MEM_W(0x264,scheduler)=pending?int32_t(0x80210000):0;
        ctx->r2=0;
    }
}
extern "C" void osRecvMesg_recomp(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r6==OS_MESG_BLOCK,"audio loop changed to a nonblocking wait");
    if(uint32_t(ctx->r4)==uint32_t(wake)) {
        if(!MEM_W(8,wake)) {
            MEM_W(0,wake)=int32_t(audio_thread);
            drain_commands(rdram);check(pending,"no pending task at the next VI");
            notify(rdram);
        }
        check(MEM_W(8,wake)>0,"idle audio thread was not woken");
        --MEM_W(8,wake);MEM_W(0,wake)=0;MEM_W(0,ctx->r5)=5;
    } else {
        check(uint32_t(ctx->r4)==uint32_t(done),"unexpected audio wait queue");
        if(!active) {
            drain_commands(rdram);notify(rdram);
            check(pending>=2,"stall did not retain the observed unstarted tasks");
            throw Stalled{};
        }
        --active;++completed;MEM_W(0,ctx->r5)=int32_t(0x80210068);
    }
    ctx->r2=0;
}
extern "C" void func_global_asm_80601EE4(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==gpr(int32_t(0x80200000+(MEM_W(0,0xffffffff807452c0ULL)%3)*0x100)),"audio buffer rotation changed");
    // The real builder advances this counter in its DMA-maintenance helper.
    ++MEM_W(0,0xffffffff807452c0ULL);++commands;++built;ctx->r2=1;
}
extern "C" void func_global_asm_80602104(uint8_t *,recomp_context *) {}
extern "C" void func_global_asm_8060A500(uint8_t *,recomp_context *) { if(completed==120)throw Finished{}; }
extern "C" void n_alClose(uint8_t *,recomp_context *) { throw std::runtime_error("audio thread exited unexpectedly"); }

int main() {
    try {
        for(bool use_guard:{false,true}) {
            std::vector<uint8_t> memory(8*1024*1024);auto *rdram=memory.data();
            guarded=use_guard;commands=pending=active=built=completed=extra_wakes=0;
            for(unsigned i=0;i<3;++i)MEM_W(i*4,0xffffffff8076d4d8ULL)=int32_t(0x80200000+i*0x100);
            recomp_context ctx{};ctx.r29=0xffffffff80600000ULL;
            bool stalled=false,finished=false;
            try { func_global_asm_80601D24(rdram,&ctx); }
            catch(const Stalled &) { stalled=true; }
            catch(const Finished &) { finished=true; }
            if(use_guard) {
                check(finished&&!stalled&&completed==120&&built==121,"idle-receiver guard lost audio liveness");
                check(extra_wakes==1,"guard sent a duplicate wakeup to a busy receiver"); // Startup only.
            } else {
                check(stalled&&!finished&&built==3&&completed==1&&pending==2,"fixture did not reproduce captured deadlock");
            }
            std::printf("Audio wakeup: guard=%d built=%u completed=%u pending=%u stalled=%d\n",use_guard,built,completed,pending,stalled);
        }
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
