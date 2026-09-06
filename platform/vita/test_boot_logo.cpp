#include "recomp.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

extern "C" void dk64_vita_present_boot_logo(uint8_t *,recomp_context *);
namespace {
constexpr gpr mode=0xffffffff80744510ULL,stack=0xffffffff80400000ULL;
constexpr uint64_t vi_cycles=781250;
uint64_t clock_cycles=0,sound_time=0;
unsigned waits=0,sounds=0,ready_after=0,cancel_after=0;
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
}
extern "C" uint64_t osGetTime() { return clock_cycles; }
extern "C" void dk64_vita_frame_wait(uint8_t *rdram) {
    ++waits; clock_cycles+=vi_cycles;
    if(ready_after && waits==ready_after) MEM_B(0,mode)=2;
    if(cancel_after && waits==cancel_after) MEM_B(0,mode)=0;
    check(waits<200,"boot logo wait did not finish");
}
extern "C" void playSound(uint8_t *rdram,recomp_context *ctx) {
    ++sounds; sound_time=clock_cycles;
    check(MEM_BU(0,mode)!=1,"startup sound played before the logo was unblanked");
    check(ctx->r4==0x23c && ctx->r5==0x7fff && ctx->r6==0x427c0000 && ctx->r7==0x3f800000,
        "startup sound parameters changed");
    check(ctx->r29==stack-0x20 && MEM_W(0x10,ctx->r29)==0 && MEM_W(0x14,ctx->r29)==0,
        "startup sound did not use a private outgoing argument area");
    ctx->r2=123; ctx->r16=456; ctx->f0.u64=0xabcdef;
    // A copied context must not retain aliases into its caller's FPRs.
    ctx->f_odd[0]=0x12345678; ctx->f_odd[8]=0x87654321;
}
int main() {
    try {
        struct Case { unsigned mode,ready,cancel,expected_waits,sound_waits; uint64_t start; };
        const Case cases[]={{1,3,0,87,3,0},{2,0,0,84,0,0},{0,0,0,0,0,0},
            {1,0,2,2,2,0},{2,0,4,4,0,0},{2,0,0,84,0,UINT64_MAX-2*vi_cycles}};
        for(const auto &test:cases) for(unsigned float_mode:{0u,1u}) {
            std::vector<uint8_t> memory(8*1024*1024);auto *rdram=memory.data();
            recomp_context ctx{};ctx.r29=stack;ctx.r31=0xffffffff80600000ULL;
            ctx.r2=2;ctx.r16=16;ctx.f0.u64=0x1020304050607080ULL;
            ctx.mips3_float_mode=float_mode;ctx.f_odd=float_mode?&ctx.f1.u32l:&ctx.f0.u32h;
            const recomp_context before=ctx;
            MEM_B(0,mode)=test.mode;
            for(unsigned i=0;i<8;++i) MEM_W(i*4,stack)=0x12340000+i;
            waits=sounds=0;ready_after=test.ready;cancel_after=test.cancel;clock_cycles=test.start;
            dk64_vita_present_boot_logo(rdram,&ctx);
            check(waits==test.expected_waits && sounds==1,"boot logo wait duration or sound count changed");
            check(sound_time==test.start+test.sound_waits*vi_cycles,"startup sound ordering changed");
            check(std::memcmp(&ctx,&before,sizeof(ctx))==0,"startup sound changed caller registers or floating-point aliases");
            for(unsigned i=0;i<8;++i)check(uint32_t(MEM_W(i*4,stack))==0x12340000+i,"startup sound overwrote its caller's argument area");
        }
        std::puts("Boot logo: unblank wait, sound ordering, 1.4-second hold, cancellation, clock wrap and private caller state passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
