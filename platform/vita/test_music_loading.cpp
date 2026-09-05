// Validate the generated music loader's sizing/ownership contract; synthesis
// and the compressed data stream are outside this fixture.
#include "recomp.h"
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <vector>
extern "C" void func_global_asm_8060A1B0(uint8_t *,recomp_context *);
static unsigned channel=0,song=0,span=0,loads=0,releases=0,sequences=0,starts=0;
static bool scratch_live=false;
static constexpr gpr scratch=0xffffffff80200000ULL;
static gpr player() { return 0xffffffff80400000ULL+channel*0x100; }
static gpr destination() { return 0xffffffff80410000ULL+channel*0x10000; }
static gpr sequence() { return 0xffffffff8076bf48ULL+channel*0xf8; }
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
extern "C" void alSeqpStop(uint8_t *,recomp_context *ctx) { check(ctx->r4==player(),"wrong music player stopped"); }
extern "C" void func_global_asm_80737E30(uint8_t *,recomp_context *ctx) { ctx->r2=0; }
extern "C" void _malloc(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==0x8000 && !scratch_live,"music workspace allocation changed"); scratch_live=true; ctx->r2=scratch;
}
extern "C" void _free(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==scratch && scratch_live,"music workspace released incorrectly"); scratch_live=false; ++releases;
}
extern "C" void func_global_asm_8060B140(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==0x01000000+0x1000+song*0x400,"music ROM start changed");
    check(ctx->r5==destination() && uint32_t(MEM_W(0,ctx->r6))==((span+1)&~1U),"music span or even-byte rounding changed");
    check(ctx->r7==0x80 && MEM_W(0x10,ctx->r29)==0 && MEM_W(0x14,ctx->r29)==1,"music loader flags changed");
    check(gpr(MEM_W(0x18,ctx->r29))==scratch && scratch_live,"music loader lost its live workspace");
    MEM_W(0,destination())=0x12345678; ++loads;
}
extern "C" void n_alCSeqNew(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==sequence() && ctx->r5==destination(),"sequence/player layout changed");
    check(!scratch_live && MEM_W(0,destination())==0x12345678,"music destination was lost with its workspace"); ++sequences;
}
extern "C" void alSeqpSetSeq(uint8_t *,recomp_context *ctx) { check(ctx->r4==player() && ctx->r5==sequence(),"sequence assigned to wrong player"); }
extern "C" void func_global_asm_80737F40(uint8_t *,recomp_context *ctx) { check(ctx->r4==player(),"wrong player finalized"); }
extern "C" void func_global_asm_8060A398(uint8_t *,recomp_context *ctx) { check(ctx->r4==channel,"wrong music channel updated"); }
extern "C" void func_global_asm_80737E50(uint8_t *,recomp_context *ctx) { check(ctx->r4==player(),"wrong player restarted"); ++starts; }
int main() {
    try {
        std::vector<uint8_t> memory(8*1024*1024); auto *rdram=memory.data();
        const unsigned songs[]={0,1,7,174},spans[]={32,33,511,512};
        for(channel=0;channel<4;++channel) {
            song=songs[channel]; span=spans[channel];
            MEM_W(channel*4,0xffffffff8076bf20ULL)=int32_t(player());
            MEM_W(channel*4,0xffffffff8076bf38ULL)=int32_t(destination());
            MEM_W(song*4,0xffffffff8076d200ULL)=0x1000+song*0x400;
            MEM_W((song+1)*4,0xffffffff8076d200ULL)=0x1000+song*0x400+span;
            MEM_W(0,0xffffffff8000ddccULL)=0x01000000;
            recomp_context ctx{}; ctx.r29=0xffffffff80600000ULL; ctx.r4=channel; ctx.r5=song;
            const int32_t gain=std::bit_cast<int32_t>(0.5f+channel*0.25f); ctx.r6=gain;
            func_global_asm_8060A1B0(rdram,&ctx);
            check(ctx.r29==0xffffffff80600000ULL,"music loader did not restore guest stack");
            check(MEM_BU(channel,0xffffffff80770560ULL)==song && MEM_W(channel*4,0xffffffff80770568ULL)==gain,"music channel bookkeeping changed");
        }
        check(loads==4 && releases==4 && sequences==4 && starts==4 && !scratch_live,"music loader lifecycle incomplete");
        std::puts("Music loading: table spans, even-byte rounding, four channels and workspace ownership passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
