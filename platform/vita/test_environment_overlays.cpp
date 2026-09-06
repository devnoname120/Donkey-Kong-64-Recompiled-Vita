#include "recomp.h"
#include "../host_probe/egl.h"
#include "fast/rt64_fast_interpreter.h"
#include "gbi/rt64_gbi_f3dex2.h"
#include "gbi/rt64_gbi_rdp.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void func_global_asm_80701CA0(uint8_t *,recomp_context *);
extern "C" void func_global_asm_8068D264(uint8_t *,recomp_context *);
extern "C" void osVirtualToPhysical_recomp(uint8_t *,recomp_context *ctx) { ctx->r2=uint32_t(ctx->r4)&0x1fffffff; }
extern "C" void switch_error(const char *,uint32_t,uint32_t) { throw std::runtime_error("invalid environment overlay dispatch"); }
static bool batching=false;
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }
unsigned probePauseGeneration() { return 0; }
namespace {
constexpr gpr character=0xffffffff80400000ULL,player=0xffffffff80410000ULL;
constexpr gpr display_list=0xffffffff80300000ULL,stack=0xffffffff805f0000ULL;
constexpr gpr scroll_x=0xffffffff8075022cULL,scroll_y=0xffffffff80750228ULL;
unsigned allocations=0,frees=0,textures=0,releases=0;
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
void setFloat(uint8_t *rdram,gpr at,float value) { MEM_W(0,at)=std::bit_cast<int32_t>(value); }
float getFloat(uint8_t *rdram,gpr at) { return std::bit_cast<float>(MEM_W(0,at)); }
struct Fixture {
    std::vector<uint8_t> memory=std::vector<uint8_t>(8*1024*1024);
    uint8_t *rdram=memory.data();
    std::unique_ptr<RT64::FastDrawSink> sink;
    RT64::State state;
    RT64::GBI gbi;
    RT64::Interpreter interpreter;
    recomp_context ctx{};
    explicit Fixture(ProbeEGL &platform) : sink(platform.createSink()),state(rdram,memory.size(),*sink) {
        allocations=frees=textures=releases=0;
        sink->setRDRAM(rdram,memory.size());
        gbi.ucode=RT64::GBIUCode::F3DEX2; RT64::GBI_RDP::setup(&gbi,true); RT64::GBI_F3DEX2::setup(&gbi);
        interpreter.setup(&state); interpreter.hleGBI=&gbi; state.rsp->setGBI(&gbi);
        state.rsp->setSegment(1,0x420000); state.rsp->setSegment(2,0x430000);
        for(unsigned offset:{0x40u,0x68u,0x118u}) { MEM_W(offset,0xffffffff80420000ULL)=0xdf000000;MEM_W(offset+4,0xffffffff80420000ULL)=0; }
        const std::array<float,16> identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        auto ortho=identity;ortho[0]=2.0f/319;ortho[5]=-2.0f/239;ortho[10]=-1.0f/20000;ortho[12]=-1;ortho[13]=1;
        for(unsigned offset:{0u,0x80u,0x180u,0x200u}) for(unsigned i=0;i<16;++i) {
            const int32_t fixed=int32_t((offset==0x80?ortho:identity)[i]*65536);
            MEM_H(offset+i*2,0xffffffff80430000ULL)=fixed>>16;MEM_H(offset+32+i*2,0xffffffff80430000ULL)=fixed;
        }
        // Original double constants for opacity smoothing and depth multipliers.
        for(unsigned i=0;i<3;++i) { const double values[]={0.05,0.4,0.07};SD(std::bit_cast<uint64_t>(values[i]),i*8,0xffffffff8075df30ULL); }
        MEM_W(0,0xffffffff807fc924ULL)=uint32_t(character);MEM_B(0,0xffffffff807fc928ULL)=1;
        MEM_B(0,0xffffffff8074450cULL)=1;
        MEM_H(0,0xffffffff80744490ULL)=320;MEM_H(0,0xffffffff80744494ULL)=240;
        for(unsigned i=0;i<4;++i) MEM_H(i*4,0xffffffff80744498ULL)=std::array{10,10,309,229}[i];
        state.rdp->setColorImage(G_IM_FMT_RGBA,G_IM_SIZ_32b,320,0x100000);
        state.rdp->setScissor(0,40,40,1236,916);
        RT64::FastDraw bg;bg.colorAddress=0x100000;bg.width=320;bg.height=240;bg.colorBytes=4;bg.fill=true;bg.fillColor={1,1,1,1};
        for(const auto &xy:std::array<std::array<float,2>,6>{{{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}}}) {
            RT64::FastVertex v;v.position[0]=xy[0];v.position[1]=xy[1];bg.vertices.push_back(v);
        }
        sink->draw(bg);
        ctx.f_odd=&ctx.f0.u32h;ctx.r4=display_list;ctx.r29=stack;ctx.r31=0xffffffff80500000ULL;
        ctx.r16=16;ctx.r17=17;ctx.r18=18;ctx.r23=23;
    }
    void bounds(unsigned index,const std::array<int,4> &box) {
        for(unsigned i=0;i<4;++i) MEM_H(0x270+i*2,character+index*0x2f0)=box[i];
    }
    std::vector<uint8_t> pixels() {
        check(ctx.r29==stack && ctx.r31==0xffffffff80500000ULL && ctx.r16==16 && ctx.r17==17 && ctx.r18==18 && ctx.r23==23,
            "environment overlay changed caller state");
        MEM_W(0,ctx.r2)=0xdf000000;MEM_W(4,ctx.r2)=0;
        interpreter.processDisplayLists(0x300000,reinterpret_cast<RT64::DisplayList *>(state.fromRDRAM(0x300000)));
        std::vector<uint8_t> out;check(sink->readFramebuffer(0x100000,320*240*4,out),"environment overlay readback failed");return out;
    }
};
void water(ProbeEGL &platform) {
    for(bool merge:{false,true}) for(unsigned players:{1u,2u}) for(bool narrow:{true,false}) {
        batching=merge;Fixture f(platform);auto *rdram=f.rdram;
        MEM_W(0,0xffffffff8076a0a8ULL)=7;MEM_B(0,0xffffffff807fc928ULL)=players;MEM_B(0,0xffffffff807fd890ULL)=1;
        std::array<std::array<int,4>,2> boxes;
        for(unsigned i=0;i<players;++i) {
            boxes[i]=players==2?(i?std::array{10,120,309,229}:std::array{10,10,309,120}):
                narrow?std::array{80,60,240,180}:std::array{10,10,309,229};
            f.bounds(i,boxes[i]);const gpr ch=character+i*0x2f0,p=player+i*0x1000;
            MEM_B(0,ch)=1;MEM_W(4,ch)=uint32_t(p);MEM_W(0x174,p)=uint32_t(p+0x400);
            MEM_W(0x104,p+0x400)=uint32_t(p+0x800);MEM_W(0x174,p+0x800)=uint32_t(p+0xc00);
            MEM_B(0x2e8,ch)=1;MEM_B(0xfa,p+0xc00)=1;setFloat(rdram,ch+0x24c,20*(i+1));setFloat(rdram,p+0xc90,20*(i+1));
        }
        func_global_asm_80701CA0(rdram,&f.ctx);const auto pixels=f.pixels();
        for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
            int alpha=0;
            for(unsigned i=0;i<players;++i) if(x>=unsigned(boxes[i][0]) && x<unsigned(boxes[i][2]) && y>=unsigned(boxes[i][1]) && y<unsigned(boxes[i][3])) alpha=100+8*(i+1);
            const int expected[]={255-alpha,255-alpha,int(std::lround(255-alpha+60.0*alpha/255.0))};
            for(unsigned c=0;c<3;++c) if(std::abs(int(pixels[(y*320+x)*4+c])-expected[c])>1)
                throw std::runtime_error("water tint crossed a viewport edge or changed opacity");
        }
        check(allocations==0 && frees==0,"rectangle water overlay retained an unused vertex allocation");
        for(unsigned i=0;i<players;++i) check(getFloat(rdram,player+i*0x1000+0x5e8)==8.0f*(i+1),"water opacity state changed");
    }
}
void sandstorm(ProbeEGL &platform) {
    for(bool merge:{false,true}) for(bool aztec:{false,true}) for(unsigned lag:{1u,2u,3u,8u}) for(bool wrap:{false,true}) for(bool narrow:{false,true}) {
        batching=merge;Fixture f(platform);auto *rdram=f.rdram;
        const auto box=narrow?std::array{80,60,240,180}:std::array{10,10,309,229};f.bounds(0,box);
        const float x=wrap?1.25f:200.0f,y=wrap?.25f:100.0f;
        MEM_W(0,0xffffffff8076a0a8ULL)=aztec?38:7;MEM_W(0,0xffffffff80744478ULL)=lag;
        setFloat(rdram,scroll_x,x);setFloat(rdram,scroll_y,y);setFloat(rdram,0xffffffff80450000ULL,.5f);f.ctx.r5=0xffffffff80450000ULL;
        // Original quad data, for the pre-fix regression run.
        for(unsigned i=0;i<4;++i) {
            const gpr v=0xffffffff80750230ULL+i*16;
            MEM_H(0,v)=std::array{0,320,320,0}[i];MEM_H(2,v)=std::array{0,0,240,240}[i];MEM_H(4,v)=-10;
            MEM_H(8,v)=std::array{0,2016,2016,0}[i];MEM_H(10,v)=std::array{0,0,2016,2016}[i];MEM_W(12,v)=-1;
        }
        for(unsigned ty=0;ty<64;++ty) for(unsigned tx=0;tx<64;++tx) MEM_B(ty*64+tx,0xffffffff80500000ULL)=(((tx*3+ty*5)&15)<<4)|15;
        func_global_asm_8068D264(rdram,&f.ctx);const auto pixels=f.pixels();
        const unsigned alpha=aztec?100:17;const int prim[]={aztec?138:255,aztec?82:255,aztec?22:255};
        for(unsigned py=0;py<240;++py) for(unsigned px=0;px<320;++px) {
            const bool inside=px>=unsigned(box[0]) && px<unsigned(box[2]) && py>=unsigned(box[1]) && py<unsigned(box[3]);
            const int u=int(std::floor(int(x*8)/32.0-(int(px)-box[0]+.5)));
            const int v=int(std::floor(int(y*8)/32.0+(int(py)-box[1]+.5)));
            const unsigned intensity=(((u%64+64)%64*3+(v%64+64)%64*5)&15)*17;
            for(unsigned c=0;c<3;++c) {
                const int expected=inside?int(std::lround(255-alpha+double(intensity)*prim[c]*alpha/(255.0*255.0))):255;
                if(std::abs(int(pixels[(py*320+px)*4+c])-expected)>1) throw std::runtime_error("sandstorm texel mapping or viewport coverage changed");
            }
        }
        float nx=x-(aztec?14.0f:.5f)*(lag*.5f),ny=y-(aztec?2.0f:1.0f)*(lag*.5f);
        if(nx<0)nx+=255;if(ny<0)ny+=255;
        check(getFloat(rdram,scroll_x)==nx && getFloat(rdram,scroll_y)==ny,"sandstorm scroll wrap lost fractional overshoot");
        check(textures==1 && releases==1,"sandstorm changed texture acquisition/release ownership");
    }
}
}
extern "C" void _malloc(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==64 && allocations<4,"unexpected water allocation");ctx->r2=0xffffffff80500000ULL+allocations++*0x100;
    for(unsigned i=0;i<64;++i) MEM_B(i,ctx->r2)=0xa5;
}
extern "C" void func_global_asm_8061134C(uint8_t *,recomp_context *) { ++frees; }
extern "C" void isFlagSet(uint8_t *,recomp_context *ctx) { ctx->r2=0; }
extern "C" void getPointerTableFile(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==25 && ctx->r5==0x173c && ctx->r6==1 && ctx->r7==0,"wrong sandstorm texture request");++textures;ctx->r2=0x500000;
}
extern "C" void func_global_asm_8066B434(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==0x500000 && ctx->r5==0x1b2 && ctx->r6==0x46,"wrong sandstorm texture release");++releases;
}
int main(int argc,char **argv) {
    try { auto platform=createProbeEGL(".");check(argc==2,"select water or sandstorm");
        if(std::string(argv[1])=="water") water(*platform);else if(std::string(argv[1])=="sandstorm") sandstorm(*platform);else check(false,"unknown overlay check");
        std::printf("Environment overlay %s: generated entry, state, ownership and rendered pixels passed\n",argv[1]);return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
