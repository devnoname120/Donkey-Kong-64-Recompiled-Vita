// Follow GPU-rendered pixels through the native framebuffer bridge and the
// generated photograph/crop/sepia routines. No substitute pixel converter or
// pre-read primes the source framebuffer in this fixture.
#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/renderer_context.hpp"
#include "../host_probe/egl.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_806FFF88(uint8_t *,recomp_context *);
extern "C" void func_global_asm_806FFC04(uint8_t *,recomp_context *);
namespace {
bool batching=false;
RT64::FastDrawSink *draw_sink=nullptr;
uint32_t source=0;
unsigned reads=0,allocations=0;
constexpr gpr output=0xffffffff80500000ULL;
void check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
uint16_t pixel(unsigned x,unsigned y,unsigned phase) {
    x/=16;y/=16;
    return uint16_t((((3*x+y+phase*11)&31)<<11)|(((x+5*y+phase*7)&31)<<6)
        |(((7*x+3*y+phase*13)&31)<<1)|((x+y+phase)&1));
}
uint16_t sepia(uint16_t value) {
    const unsigned sum=(value>>11)+((value>>6)&31)+((value>>1)&31);
    const unsigned r=3+28*sum/93,g=3+19*sum/93,b=sum<48?0:18*(sum-48)/45;
    return uint16_t((r<<11)|(g<<6)|(b<<1)|(value&1));
}
void drawPattern(unsigned phase) {
    RT64::FastDraw draw;draw.colorAddress=source;draw.width=320;draw.height=240;draw.colorBytes=2;
    draw.fill=true;draw.vertices.resize(6);
    const unsigned corners[]={0,1,2,0,2,3};
    for(unsigned y=0;y<240;y+=16)for(unsigned x=0;x<320;x+=16) {
        const uint16_t color=pixel(x,y,phase);
        draw.fillColor={float(color>>11)/31,float((color>>6)&31)/31,float((color>>1)&31)/31,float(color&1)};
        const float xy[4][2]={{2.0f*x/320-1,1-2.0f*y/240},{2.0f*(x+16)/320-1,1-2.0f*y/240},
            {2.0f*(x+16)/320-1,1-2.0f*(y+16)/240},{2.0f*x/320-1,1-2.0f*(y+16)/240}};
        for(unsigned i=0;i<6;++i) { draw.vertices[i].position[0]=xy[corners[i]][0];draw.vertices[i].position[1]=xy[corners[i]][1]; }
        draw_sink->draw(draw);
    }
    draw_sink->fullSync();
}
}
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }
unsigned probePauseGeneration() { return 0; }
std::vector<uint8_t> ultramodern::renderer::read_framebuffer(uint32_t address,uint32_t size) {
    check(address==source&&size==320*240*2,"photograph requested the wrong image");
    ++reads;std::vector<uint8_t> bytes;
    check(draw_sink->readFramebuffer(address,size,bytes),"rendered photograph source was not resident");
    return bytes;
}
extern "C" void _malloc(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==0xa000,"photograph allocation changed");++allocations;ctx->r2=output;
}
extern "C" void func_global_asm_806FFF5C(uint8_t *,recomp_context *) { throw std::runtime_error("unexpected texture-only photograph"); }

int main() {
    try {
        auto platform=createProbeEGL(".");unsigned photographs=0,converted=0;
        for(bool merge:{false,true}) {
            batching=merge;auto sink=platform->createSink();draw_sink=sink.get();
            std::vector<uint8_t> memory(recomp::mem_size,0xa5);auto *rdram=memory.data();
            draw_sink->setRDRAM(rdram,memory.size());
            // US overlay doubles at ROM 0x2162B80/0x2162B88: 45.0 and 93.0.
            MEM_W(0,0xffffffff8075de80ULL)=0x40468000;MEM_W(4,0xffffffff8075de80ULL)=0;
            MEM_W(0,0xffffffff8075de88ULL)=0x40574000;MEM_W(4,0xffffffff8075de88ULL)=0;
            recomp_context ctx{};ctx.f_odd=&ctx.f0.u32h;ctx.r29=0xffffffff80600000ULL;
            set_cop1_cs(0);
            for(unsigned value=0;value<65536;++value) {
                ctx.r4=value;func_global_asm_806FFC04(rdram,&ctx);
                check(ctx.r2==sepia(value),"photo color conversion differs from its sepia curve");++converted;
            }
            constexpr gpr actor=0xffffffff80401000ULL;
            MEM_W(0,0xffffffff807fbb40ULL)=int32_t(actor);MEM_W(0x58,actor)=1;
            MEM_H(0,0xffffffff80744490ULL)=320;MEM_H(0,0xffffffff80744494ULL)=240;
            for(unsigned buffer=0;buffer<2;++buffer)for(unsigned phase=0;phase<2;++phase) {
                source=0x300000+buffer*0x40000;
                const uint32_t alias=(buffer?0xa0000000U:0x80000000U)|source;
                MEM_W(buffer*4,0xffffffff80744470ULL)=alias;MEM_B(0,0xffffffff807444fcULL)=buffer;
                drawPattern(phase);
                check(MEM_HU(0,alias)!=pixel(0,0,phase),"fixture already contains the new GPU image in RAM");
                MEM_W(-4,output)=int32_t(0x12345678);MEM_W(0xa000,output)=int32_t(0x76543210);
                func_global_asm_806FFF88(rdram,&ctx);++photographs;
                check(reads==photographs&&allocations==photographs,"photo did not use exactly one readback and allocation");
                check(ctx.r2==output&&ctx.r29==0xffffffff80600000ULL,"photo result or guest stack changed");
                for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)
                    check(MEM_HU((y*320+x)*2,alias)==pixel(x,y,phase),"GPU readback changed source channels, rows or alpha");
                for(unsigned ty=0;ty<2;++ty)for(unsigned tx=0;tx<5;++tx)
                    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<32;++x) {
                        const unsigned index=((ty*5+tx)*64+y)*32+x;
                        check(MEM_HU(index*2,output)==sepia(pixel(80+tx*32+x,56+ty*64+y,phase)),"photo crop, tile order or sepia pixels are incorrect");
                    }
                check(MEM_W(-4,output)==0x12345678&&MEM_W(0xa000,output)==0x76543210,"photo wrote beyond its allocation");
            }
        }
        std::printf("Photo pipeline: %u exact GPU photographs, both framebuffer aliases, batching on/off and %u color conversions passed\n",photographs,converted);
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
