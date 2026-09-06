#include "recomp.h"
#include "../host_probe/egl.h"
#include "fast/rt64_fast_interpreter.h"
#include "gbi/rt64_gbi_f3dex2.h"
#include "gbi/rt64_gbi_rdp.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <vector>

extern "C" void func_menu_8003292C(uint8_t *,recomp_context *);
static bool batching=false;
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }
unsigned probePauseGeneration() { return 0; }
namespace {
constexpr gpr dl=0xffffffff80300000ULL,stack=0xffffffff805f0000ULL,texture=0xffffffff80500000ULL,text=0xffffffff80400000ULL;
std::vector<std::array<gpr,4>> text_calls;
unsigned string_calls=0,texture_calls=0,styled_calls=0;
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
}
extern "C" void getTextString(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==13 && ctx->r5==string_calls++ && ctx->r6==1,"wrong legal text request");ctx->r2=text+ctx->r5*0x100;
}
extern "C" void func_global_asm_806FD894(uint8_t *,recomp_context *ctx) { check(ctx->r4==1,"wrong legal font metrics");ctx->r2=16; }
extern "C" void func_global_asm_8068C12C(uint8_t *,recomp_context *ctx) { check(ctx->r4==0xb0,"wrong Dolby texture request");++texture_calls;ctx->r2=texture; }
extern "C" void getCenterOfString(uint8_t *,recomp_context *ctx) { check(ctx->r4==1 && ctx->r5==text,"wrong legal text centering");ctx->r2=120; }
extern "C" void printText(uint8_t *rdram,recomp_context *ctx) { text_calls.push_back({ctx->r5,ctx->r6,ctx->r7,gpr(MEM_W(16,ctx->r29))});ctx->r2=ctx->r4; }
extern "C" void printStyledText(uint8_t *,recomp_context *ctx) { ++styled_calls;ctx->r2=ctx->r4; }
int main() {
    try {
        auto platform=createProbeEGL(".");
        for(bool merge:{false,true}) for(unsigned timer:{0u,1u,16u,17u,255u}) {
            batching=merge;string_calls=texture_calls=styled_calls=0;text_calls.clear();
            std::vector<uint8_t> memory(8*1024*1024);auto *rdram=memory.data();
            MEM_H(0,0xffffffff80744490ULL)=320;MEM_H(0,0xffffffff80744494ULL)=240;MEM_B(0,0xffffffff8074450cULL)=1;MEM_B(0,0xffffffff800339d0ULL)=timer;
            // Four 4b tiles, with row/column-varying nonzero intensity to expose
            // incorrect scaling, tile seams and source-coordinate selection.
            for(unsigned y=0;y<83;++y) for(unsigned x=0;x<204;x+=2) {
                const auto intensity=[](unsigned a,unsigned b){return 1+(a*3+b*5)%15;};
                MEM_B(y*102+x/2,texture)=(intensity(x,y)<<4)|intensity(x+1,y);
            }
            MEM_W(0,0xffffffff80420118ULL)=0xdf000000;MEM_W(4,0xffffffff80420118ULL)=0;
            const std::array<float,16> matrix={2.0f/1279,0,0,0,0,-2.0f/959,0,0,0,0,-1.0f/20000,0,-1,1,0,1};
            for(unsigned i=0;i<16;++i){const int32_t v=int32_t(matrix[i]*65536);MEM_H(i*2,0xffffffff804300c0ULL)=v>>16;MEM_H(32+i*2,0xffffffff804300c0ULL)=v;}
            auto sink=platform->createSink();sink->setRDRAM(rdram,memory.size());RT64::State state(rdram,memory.size(),*sink);RT64::GBI gbi;
            gbi.ucode=RT64::GBIUCode::F3DEX2;RT64::GBI_RDP::setup(&gbi,true);RT64::GBI_F3DEX2::setup(&gbi);RT64::Interpreter interpreter;
            interpreter.setup(&state);interpreter.hleGBI=&gbi;state.rsp->setGBI(&gbi);state.rsp->setSegment(1,0x420000);state.rsp->setSegment(2,0x430000);
            state.rdp->setColorImage(G_IM_FMT_RGBA,G_IM_SIZ_32b,320,0x100000);state.rdp->setScissor(0,0,0,1280,960);
            RT64::FastDraw background;background.colorAddress=0x100000;background.width=320;background.height=240;background.colorBytes=4;background.fill=true;background.fillColor={0,0,0,1};
            for(const auto &xy:std::array<std::array<float,2>,6>{{{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}}}){RT64::FastVertex v;v.position[0]=xy[0];v.position[1]=xy[1];background.vertices.push_back(v);}sink->draw(background);
            recomp_context ctx{};ctx.f_odd=&ctx.f0.u32h;ctx.r4=dl;ctx.r29=stack;ctx.r31=0xffffffff80510000ULL;ctx.r16=16;ctx.r17=17;
            func_menu_8003292C(rdram,&ctx);
            check(ctx.r29==stack && ctx.r31==0xffffffff80510000ULL && ctx.r16==16 && ctx.r17==17,"legal screen changed caller state");
            check(string_calls==3 && texture_calls==1,"legal screen changed asset requests");
            MEM_W(0,ctx.r2)=0xdf000000;MEM_W(4,ctx.r2)=0;
            interpreter.processDisplayLists(0x300000,reinterpret_cast<RT64::DisplayList *>(state.fromRDRAM(0x300000)));
            std::vector<uint8_t> pixels;check(sink->readFramebuffer(0x100000,320*240*4,pixels),"Dolby logo readback failed");
            const unsigned alpha=std::min(timer*15,255u);
            for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x){
                int expected=0;
                if(x>=109 && x<211 && y>=99 && y<139){const unsigned tx=(x-109)*2+1,ty=(y-99)*2+(y>=119?1:0)+1;expected=std::lround((1+(tx*3+ty*5)%15)*17*alpha/255.0);}
                for(unsigned c=0;c<3;++c)if(std::abs(int(pixels[(y*320+x)*4+c])-expected)>1)throw std::runtime_error("Dolby logo retained 480i size or incorrect 240p texel mapping");
            }
            const std::vector<std::array<gpr,4>> expected_text={{{730,357,0x3f000000,text}},{{650,800,0x3e800000,text+0x100}},{{650,817,0x3e800000,text+0x200}}};
            check(styled_calls==0 && text_calls==expected_text,"legal text retained the 480i position, scale or renderer");
        }
        std::puts("Boot legal screen: generated entry, Dolby pixels, text arguments and asset ownership passed");return 0;
    } catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
