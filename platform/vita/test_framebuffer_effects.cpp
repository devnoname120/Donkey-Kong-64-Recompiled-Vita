#include "recomp.h"
#include <array>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void func_global_asm_80629300(uint8_t *,recomp_context *);
namespace {
constexpr gpr lag=0xffffffff80744478ULL,initialized=0xffffffff80747b24ULL;
constexpr gpr cooldown=0xffffffff80747b20ULL,framebuffer_slot=0xffffffff807f5d80ULL;
constexpr gpr phase=0xffffffff807f5d84ULL,kind=0xffffffff807f5d85ULL;
constexpr gpr progress_a=0xffffffff807f5d86ULL,progress_b=0xffffffff807f5d88ULL;
constexpr gpr radius=0xffffffff807f5d8cULL,radius_end=0xffffffff807f5d90ULL;
constexpr gpr angle=0xffffffff807f5d94ULL,properties=0xffffffff807fbb60ULL;
constexpr gpr framebuffer=0xffffffff80500000ULL,display_list=0xffffffff80300000ULL;
constexpr gpr stack=0xffffffff806f0000ULL,return_address=0xffffffff80600000ULL;
unsigned draws=0,blurs=0,scales=0;
bool change_lag_during_draw=false;
std::vector<gpr> freed;
std::string scenario;
void check(bool value,const char *message) {
    if(!value) throw std::runtime_error(scenario+": "+message);
}
void setFloat(uint8_t *rdram,gpr address,float value) { MEM_W(0,address)=std::bit_cast<int32_t>(value); }
float getFloat(uint8_t *rdram,gpr address) { return std::bit_cast<float>(MEM_W(0,address)); }
void draw(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r5==framebuffer,"effect stopped using its original framebuffer allocation");
    check(ctx->r4==stack,"effect passed the wrong display-list pointer");
    const uint32_t dl=MEM_W(0,ctx->r4);
    check(dl>=uint32_t(display_list) && dl<uint32_t(display_list)+4096,"invalid display-list cursor");
    MEM_W(0,ctx->r4)=dl+8;
    ++draws;
}
struct Fixture {
    std::vector<uint8_t> memory=std::vector<uint8_t>(8*1024*1024);
    uint8_t *rdram=memory.data();
    recomp_context ctx{};
    Fixture(unsigned effect,unsigned frame_lag) {
        scenario="effect "+std::to_string(effect)+", lag "+std::to_string(frame_lag);
        draws=blurs=scales=0; freed.clear(); change_lag_during_draw=false;
        ctx.f_odd=&ctx.f0.u32h;
        ctx.r29=stack; ctx.r31=return_address;
        ctx.r16=16; ctx.r17=17; ctx.r18=18; ctx.r19=19;
        ctx.r20=20; ctx.r21=21; ctx.r22=22; ctx.r23=23;
        ctx.r28=28; ctx.r30=30; ctx.f20.u64=0x123456789abcdef0ULL;
        MEM_W(0,lag)=frame_lag; MEM_W(0,initialized)=1;
        MEM_W(0,framebuffer_slot)=uint32_t(framebuffer);
        MEM_B(0,phase)=1; MEM_B(0,kind)=effect;
        MEM_H(0,progress_a)=100; MEM_H(0,progress_b)=200;
        setFloat(rdram,radius,10.25f); setFloat(rdram,radius_end,50.25f);
        setFloat(rdram,angle,10.25f);
        MEM_H(0,0xffffffff80744490ULL)=320; MEM_H(0,0xffffffff80744494ULL)=240;
        MEM_H(0,0xffffffff807444a0ULL)=320; MEM_H(0,0xffffffff807444a4ULL)=240;
        MEM_W(0x20,stack)=0x12345678;
    }
    void step() {
        ctx.r4=display_list;
        func_global_asm_80629300(rdram,&ctx);
        check(ctx.r29==stack && ctx.r31==return_address,"effect damaged its stack or return address");
        check(std::array{ctx.r16,ctx.r17,ctx.r18,ctx.r19,ctx.r20,ctx.r21,ctx.r22,ctx.r23}
            ==std::array<gpr,8>{16,17,18,19,20,21,22,23},"effect clobbered callee-saved registers");
        check(ctx.r28==28 && ctx.r30==30 && ctx.f20.u64==0x123456789abcdef0ULL,"effect clobbered preserved register state");
        check(MEM_W(0x20,stack)==0x12345678,"effect wrote above its caller argument area");
    }
};
}
extern "C" void switch_error(const char *,uint32_t,uint32_t) { check(false,"invalid generated switch"); }
extern "C" void guScale(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==0xffffffff807f5d98ULL && ctx->r5==0x40000000 && ctx->r6==0x40000000
        && ctx->r7==0x3f800000,"effect matrix initialization changed");
    ++scales;
}
extern "C" void func_global_asm_805FD030(uint8_t *rdram,recomp_context *ctx) {
    ctx->r2=ctx->r4;
    // The function must retain the lag sampled before rendering its effects.
    if(change_lag_during_draw) MEM_W(0,lag)=8;
}
extern "C" void func_global_asm_8061134C(uint8_t *,recomp_context *ctx) { freed.push_back(ctx->r4); }
extern "C" void func_global_asm_8062A3F0(uint8_t *,recomp_context *) { ++blurs; }
extern "C" void func_global_asm_807023E8(uint8_t *rdram,recomp_context *ctx) { draw(rdram,ctx); }
extern "C" void func_global_asm_807024E0(uint8_t *rdram,recomp_context *ctx) { draw(rdram,ctx); }
int main() {
    try {
        // Literal expected updates include odd lags and the fade's truncation
        // after subtraction: 100 - 7.5 becomes 92, not 93.
        struct Expected { unsigned lag; int right,fade,left; float iris,diagonal,clock; };
        const Expected cases[]={{1,95,97,105,12.75f,16.25f,17.75f},
            {2,90,95,110,15.25f,22.25f,25.25f},{3,85,92,115,17.75f,28.25f,32.75f},
            {5,75,87,125,22.75f,40.25f,47.75f},{8,60,80,140,30.25f,58.25f,70.25f}};
        for(const auto &expected:cases) for(unsigned effect=0;effect<8;++effect) {
            Fixture f(effect,expected.lag); auto *rdram=f.rdram;
            f.step();
            check(MEM_B(0,phase)==1 && freed.empty(),"active effect completed or freed early");
            check(draws>0 && f.ctx.r2>display_list && f.ctx.r2<display_list+4096,"active effect emitted no display list");
            const int a=effect==0?expected.right:effect==1?expected.fade:
                (effect==2 || effect==3)?expected.left:100;
            check(MEM_H(0,progress_a)==a,"incorrect fade or horizontal wipe progression");
            check(MEM_H(0,progress_b)==(effect==3?expected.right+100:200),"incorrect second wipe progression");
            const float r=effect==4?expected.iris:effect==5?expected.diagonal:10.25f;
            check(getFloat(rdram,radius)==r && getFloat(rdram,radius_end)==r+40.0f,"incorrect radial wipe progression");
            check(getFloat(rdram,angle)==(effect==6?expected.clock:10.25f),"incorrect clock wipe progression");
            check(blurs==(effect==7?1u:0u),"original pause blur call changed");
        }
        // Every transition completes at its original strict/inclusive boundary
        // and keeps the two-frame deferred release of its owned framebuffer.
        struct Boundary { unsigned effect; float before; bool complete; };
        const Boundary boundaries[]={{0,26,false},{0,25,true},{1,7,false},{1,6,true},
            {2,295,false},{2,296,true},{3,295,false},{3,296,true},
            {4,162.5f,false},{4,163,true},{5,332,false},{5,333,true},{6,327.5f,false},{6,328,true}};
        for(const auto &boundary:boundaries) {
            Fixture f(boundary.effect,3); auto *rdram=f.rdram;
            if(boundary.effect<4) MEM_H(0,progress_a)=int(boundary.before);
            else setFloat(rdram,boundary.effect==6?angle:radius,boundary.before);
            f.step();
            check(MEM_B(0,phase)==(boundary.complete?-2:1),"transition completion boundary changed");
            if(!boundary.complete) continue;
            const auto completed_draws=draws;
            f.step();
            check(MEM_B(0,phase)==-1 && freed.empty() && draws==completed_draws,"first cleanup frame drew or released the buffer");
            f.step();
            check(MEM_B(0,phase)==0 && freed==std::vector<gpr>{framebuffer} && draws==completed_draws,"second cleanup frame did not release the owned buffer once");
            MEM_B(0,cooldown)=1;
            f.step(); f.step();
            check(MEM_BU(0,cooldown)==0 && freed.size()==1 && draws==completed_draws && f.ctx.r2==display_list,"inactive effect underflowed cooldown, drew or freed again");
        }
        for(unsigned effect=0;effect<7;++effect) {
            Fixture f(effect,3); auto *rdram=f.rdram;
            change_lag_during_draw=true;
            f.step();
            const int a=effect==0?85:effect==1?92:(effect==2 || effect==3)?115:100;
            check(MEM_H(0,progress_a)==a && MEM_H(0,progress_b)==(effect==3?185:200),"integer effect resampled lag during rendering");
            check(getFloat(rdram,radius)==(effect==4?17.75f:effect==5?28.25f:10.25f)
                && getFloat(rdram,angle)==(effect==6?32.75f:10.25f),"floating effect resampled lag during rendering");
        }
        {
            Fixture f(7,3); auto *rdram=f.rdram;
            MEM_W(0,initialized)=0;
            f.step(); f.step();
            check(scales==1 && MEM_W(0,initialized)==1 && blurs==2,"matrix was not initialized exactly once");
            MEM_W(0,properties)=0x40;
            f.step();
            check(MEM_B(0,phase)==-2 && blurs==3,"pause exit no longer begins cleanup");
        }
        std::puts("Framebuffer effects: generated fades/wipes, sampled fractional lag, completion, cleanup, pause and guest registers passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
