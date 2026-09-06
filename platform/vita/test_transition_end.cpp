#include "recomp.h"
#include "../host_probe/egl.h"
#include "fast/rt64_fast_interpreter.h"
#include "gbi/rt64_gbi_f3dex2.h"
#include "gbi/rt64_gbi_rdp.h"
#include <array>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_80704484(uint8_t *,recomp_context *);
namespace {
bool batching=false;
unsigned blanks=0,spins=0;
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
void sf(uint8_t *rdram,gpr address,float value) { MEM_W(0,address)=std::bit_cast<uint32_t>(value); }
}
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }
unsigned probePauseGeneration() { return 0; }
extern "C" void func_global_asm_805FF000(uint8_t *,recomp_context *) {}
extern "C" void func_global_asm_80600454(uint8_t *,recomp_context *ctx) { ctx->r2=0; }
extern "C" void getTextString(uint8_t *,recomp_context *) { throw std::runtime_error("Unexpected transition title request"); }
extern "C" void func_global_asm_8069D2AC(uint8_t *,recomp_context *) { throw std::runtime_error("Unexpected transition title draw"); }
extern "C" void func_global_asm_80602B60(uint8_t *,recomp_context *) {}
extern "C" void isIntroStoryPlaying(uint8_t *,recomp_context *ctx) { ctx->r2=0; }
extern "C" void setIntroStoryPlaying(uint8_t *,recomp_context *) { throw std::runtime_error("Unexpected story change"); }
extern "C" void osViBlack_recomp(uint8_t *,recomp_context *ctx) { check(ctx->r4==1,"Unexpected VI unblank");++blanks; }
extern "C" void func_global_asm_80703850(uint8_t *,recomp_context *) { throw std::runtime_error("Unexpected viewport closing"); }
extern "C" void func_global_asm_80703AB0(uint8_t *,recomp_context *) { throw std::runtime_error("Unexpected partial fade"); }
extern "C" void func_global_asm_80703CF8(uint8_t *,recomp_context *ctx) { ++spins;ctx->r2=ctx->r4; }
extern "C" void func_global_asm_8070B324(uint8_t *rdram,recomp_context *ctx) {
    check(MEM_W(0,ctx->r6)==1 && MEM_W(4,ctx->r6)==3 && MEM_W(8,ctx->r6)==0,
        "Transition changed its original audio sequence list");
}

int main() {
    try {
        auto platform=createProbeEGL(".");
        struct Case { float progress,speed;unsigned type;bool black;unsigned vi,spin; };
        std::vector<Case> cases;
        for(unsigned type=0;type<5;++type) for(float progress:{30.5f,31.0f})
            cases.push_back({progress,1,type,true,1,0});
        cases.push_back({30,1,0,true,0,0}); // Exactly 31: cover already exists in the active path.
        cases.push_back({31,0,0,true,0,0}); // Following stopped-state call.
        cases.push_back({0,-1,0,false,0,0}); // Opening completion must not be covered.
        cases.push_back({25,0.1f,1,false,0,1}); // Retain original late-spin dispatch.
        for(bool merge:{false,true}) for(const auto &test:cases) {
            batching=merge;blanks=spins=0;
            std::vector<uint8_t> memory(8*1024*1024);auto *rdram=memory.data();
            auto sink=platform->createSink();sink->setRDRAM(rdram,memory.size());
            RT64::State state(rdram,memory.size(),*sink);
            RT64::GBI gbi;gbi.ucode=RT64::GBIUCode::F3DEX2;
            RT64::GBI_RDP::setup(&gbi,true);RT64::GBI_F3DEX2::setup(&gbi);
            RT64::Interpreter interpreter;interpreter.setup(&state);interpreter.hleGBI=&gbi;state.rsp->setGBI(&gbi);
            constexpr std::array bounds={10,10,309,229};
            for(unsigned i=0;i<4;++i) MEM_H(i*4,0xffffffff80744498ULL)=bounds[i];
            MEM_W(0,0xffffffff80754cc8ULL)=1;MEM_W(4,0xffffffff80754cc8ULL)=3;MEM_W(8,0xffffffff80754cc8ULL)=0;
            MEM_W(0,0xffffffff8076a0a8ULL)=176;MEM_B(0,0xffffffff8076a0b1ULL)=0x81;
            sf(rdram,0xffffffff807fd888ULL,test.progress);sf(rdram,0xffffffff807fd88cULL,test.speed);
            state.rdp->setColorImage(G_IM_FMT_RGBA,G_IM_SIZ_32b,320,0x100000);
            state.rdp->setScissor(0,40,40,1236,916);
            RT64::FastDraw bg;bg.colorAddress=0x100000;bg.width=320;bg.height=240;bg.colorBytes=4;
            bg.fill=true;bg.fillColor={1,1,1,1};
            for(const auto &xy:std::array<std::array<float,2>,6>{{{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}}}) {
                RT64::FastVertex v;v.position[0]=xy[0];v.position[1]=xy[1];bg.vertices.push_back(v);
            }
            sink->draw(bg);
            constexpr gpr cursor=0xffffffff80300000ULL,stack=0xffffffff80600000ULL;
            recomp_context ctx{};ctx.f_odd=&ctx.f0.u32h;ctx.r4=cursor;ctx.r5=test.type;
            ctx.r29=stack;ctx.r31=0xffffffff80500000ULL;ctx.r16=16;ctx.r23=23;
            MEM_W(0x10,stack)=0x12345678;
            func_global_asm_80704484(rdram,&ctx);
            check(ctx.r29==stack && ctx.r31==0xffffffff80500000ULL && ctx.r16==16 && ctx.r23==23,
                "Transition changed its caller state");
            check(MEM_W(0x10,stack)==0x12345678,"Transition cover overwrote caller locals");
            check(blanks==test.vi && spins==test.spin,"Transition changed VI or spin dispatch");
            check(ctx.r2>=cursor && ctx.r2<cursor+0x1000,"Transition returned an invalid display-list pointer");
            MEM_W(0,ctx.r2)=0xdf000000;MEM_W(4,ctx.r2)=0;
            interpreter.processDisplayLists(0x300000,reinterpret_cast<RT64::DisplayList *>(state.fromRDRAM(0x300000)));
            std::vector<uint8_t> pixels;check(sink->readFramebuffer(0x100000,320*240*4,pixels),"Transition readback failed");
            for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
                const bool inside=x>=10 && x<309 && y>=10 && y<229;
                const unsigned expected=test.black&&inside?0:255;
                for(unsigned c=0;c<3;++c) check(pixels[(y*320+x)*4+c]==expected,
                    "Transition framebuffer was not covered on the completion frame");
            }
        }
        std::puts("Transition end: generated manager, crossing/exact/stopped/opening states, original spin/audio dispatch, black pixels and batching passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
