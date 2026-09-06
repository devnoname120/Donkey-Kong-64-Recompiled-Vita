#include "recomp.h"
#include "../host_probe/egl.h"
#include "fast/rt64_fast_interpreter.h"
#include "gbi/rt64_gbi_f3dex2.h"
#include "gbi/rt64_gbi_rdp.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_80703AB0(uint8_t *,recomp_context *);
extern "C" void func_global_asm_807035C4(uint8_t *,recomp_context *);
static bool batching=false;
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }
unsigned probePauseGeneration() { return 0; }
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
static void matrix(uint8_t *rdram,gpr address,const std::array<float,16> &values) {
    for(unsigned i=0;i<16;++i) {
        const int32_t fixed=int32_t(values[i]*65536.0f);
        MEM_H(i*2,address)=fixed>>16; MEM_H(32+i*2,address)=fixed;
    }
}
int main() {
    try {
        auto platform=createProbeEGL(".");
        for(bool merge:{false,true}) for(unsigned bank:{0u,1u})
            for(unsigned alpha:{64u,0u,1u,127u,128u,255u}) for(bool narrow:{false,true}) {
                batching=merge;
                std::vector<uint8_t> memory(8*1024*1024); auto *rdram=memory.data();
                auto sink=platform->createSink(); sink->setRDRAM(rdram,memory.size());
                RT64::State state(rdram,memory.size(),*sink);
                RT64::GBI gbi; gbi.ucode=RT64::GBIUCode::F3DEX2;
                RT64::GBI_RDP::setup(&gbi,true); RT64::GBI_F3DEX2::setup(&gbi);
                RT64::Interpreter interpreter; interpreter.setup(&state); interpreter.hleGBI=&gbi; state.rsp->setGBI(&gbi);
                state.rsp->setSegment(1,0x400000); state.rsp->setSegment(2,0x410000);
                MEM_W(0,0xffffffff80400118ULL)=0xdf000000; // Relevant initial state is supplied explicitly below.
                MEM_W(4,0xffffffff80400118ULL)=0;
                const std::array<float,16> identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                auto ortho=identity; ortho[0]=2.0f/319.0f; ortho[5]=-2.0f/239.0f;
                ortho[10]=-1.0f/20000.0f; ortho[12]=-1; ortho[13]=1;
                matrix(rdram,0xffffffff80410080ULL,ortho); matrix(rdram,0xffffffff80410180ULL,identity);
                // Original mutable fade quad. The generated function updates
                // its alpha and dimensions; the corrected rectangle needs no vertices.
                const int xy[4][2]={{0,0},{320,0},{320,240},{0,240}};
                for(unsigned b=0;b<2;++b) for(unsigned i=0;i<4;++i) {
                    const gpr at=0xffffffff80754c48ULL+b*64+i*16;
                    MEM_H(0,at)=xy[i][0]; MEM_H(2,at)=xy[i][1]; MEM_H(4,at)=0;
                }
                MEM_H(0,0xffffffff80744490ULL)=320; MEM_H(0,0xffffffff80744494ULL)=240;
                MEM_B(0,0xffffffff807444fcULL)=bank;
                const std::array global_bounds={10,10,309,229};
                for(unsigned i=0;i<4;++i) MEM_H(i*4,0xffffffff80744498ULL)=global_bounds[i];
                const auto scissor=narrow?std::array{80,60,240,180}:global_bounds;
                state.rdp->setColorImage(G_IM_FMT_RGBA,G_IM_SIZ_32b,320,0x100000);
                state.rdp->setScissor(0,scissor[0]*4,scissor[1]*4,scissor[2]*4,scissor[3]*4);
                RT64::FastDraw background; background.colorAddress=0x100000;
                background.width=320; background.height=240; background.colorBytes=4;
                background.fill=true; background.fillColor={1,1,1,1};
                const float xy_clip[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
                for(const auto &point:xy_clip) { RT64::FastVertex v; v.position[0]=point[0];v.position[1]=point[1];background.vertices.push_back(v); }
                sink->draw(background);
                recomp_context ctx{};ctx.f_odd=&ctx.f0.u32h;
                ctx.r4=0xffffffff80300000ULL;ctx.r5=alpha;ctx.r29=0xffffffff80600000ULL;
                ctx.r31=0xffffffff80500000ULL;ctx.r16=16;ctx.r23=23;
                func_global_asm_80703AB0(rdram,&ctx);
                check(ctx.r29==0xffffffff80600000ULL && ctx.r31==0xffffffff80500000ULL && ctx.r16==16 && ctx.r23==23,
                    "cutscene fade changed its caller state");
                MEM_W(0,ctx.r2)=0xdf000000; MEM_W(4,ctx.r2)=0;
                interpreter.processDisplayLists(0x300000,reinterpret_cast<RT64::DisplayList *>(state.fromRDRAM(0x300000)));
                std::vector<uint8_t> pixels;
                check(sink->readFramebuffer(0x100000,320*240*4,pixels),"cutscene fade framebuffer read failed");
                for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
                    const bool affected=x>=unsigned(scissor[0]) && x<unsigned(scissor[2]) && y>=unsigned(scissor[1]) && y<unsigned(scissor[3]);
                    const int expected=affected?255-alpha:255;
                    for(unsigned c=0;c<3;++c) if(std::abs(int(pixels[(y*320+x)*4+c])-expected)>1)
                        throw std::runtime_error("cutscene fade did not use uniform supplied alpha inside the inherited scissor");
                }
                sink->draw(background);
                constexpr gpr character=0xffffffff80420000ULL;
                MEM_W(0,0xffffffff807fc924ULL)=uint32_t(character);
                for(unsigned i=0;i<4;++i) MEM_H(i*2,character+0x270)=scissor[i];
                MEM_W(0,0xffffffff80746a40ULL)=0x12345678u+alpha+(bank<<16);
                ctx.r4=0xffffffff80300000ULL; ctx.r5=0; ctx.r20=0x1111; ctx.r21=0x2222;
                func_global_asm_807035C4(rdram,&ctx);
                check(ctx.r29==0xffffffff80600000ULL && ctx.r31==0xffffffff80500000ULL
                    && ctx.r20==0x1111 && ctx.r21==0x2222,"static overlay changed caller state");
                MEM_W(0,ctx.r2)=0xdf000000; MEM_W(4,ctx.r2)=0;
                interpreter.processDisplayLists(0x300000,reinterpret_cast<RT64::DisplayList *>(state.fromRDRAM(0x300000)));
                check(sink->readFramebuffer(0x100000,320*240*4,pixels),"static overlay framebuffer read failed");
                unsigned changed_inside=0;
                for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
                    const bool affected=x>=unsigned(scissor[0]) && x<unsigned(scissor[2]) && y>=unsigned(scissor[1]) && y<unsigned(scissor[3]);
                    const unsigned at=(y*320+x)*4;
                    const bool changed=pixels[at]<254 || pixels[at+1]<254 || pixels[at+2]<254;
                    if(!affected && changed) throw std::runtime_error("static overlay spilled outside the current viewport");
                    if(affected && changed) ++changed_inside;
                }
                check(changed_inside>0,"static overlay emitted no visible noise or stripes");
            }
        std::puts("Overlays: generated entries, F3DEX2 interpretation, uniform fade alpha, static viewport bounds, original RNG, both banks and batching passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
