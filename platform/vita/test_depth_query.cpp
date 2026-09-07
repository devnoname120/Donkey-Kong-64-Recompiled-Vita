#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/renderer_context.hpp"
#include "../host_probe/egl.h"
#include "fast/tests/rt64_fast_depth_checks.h"

extern "C" void func_global_asm_80700AE4(uint8_t *,recomp_context *);
namespace {
bool batching=false,short_read=false;
RT64::FastDrawSink *sink=nullptr;
unsigned reads=0;
uint32_t requested=0;
constexpr gpr view=0xffffffff80401000ULL,stack=0xffffffff80600000ULL;
void check(bool condition,const char *message) { if(!condition)throw std::runtime_error(message); }
void setup(uint8_t *rdram,uint32_t alias) {
    MEM_W(0,0xffffffff807fc924ULL)=int32_t(view);
    MEM_H(0x270,view)=0;MEM_H(0x272,view)=0;MEM_H(0x274,view)=31;MEM_H(0x276,view)=23;
    MEM_W(0,0xffffffff8076a060ULL)=int32_t(alias);
    MEM_H(0,0xffffffff80744490ULL)=32;MEM_H(0,0xffffffff80744494ULL)=24;
    constexpr uint32_t shifts[]={6,5,4,3,2,1,0,0};
    constexpr uint32_t biases[]={0,0x20000,0x30000,0x38000,0x3c000,0x3e000,0x3f000,0x3f800};
    for(unsigned i=0;i<8;++i) {
        MEM_W(i*8,0xffffffff80754bc4ULL)=shifts[i];
        MEM_W(i*8+4,0xffffffff80754bc4ULL)=biases[i];
    }
}
uint32_t query(uint8_t *rdram,int x,int y,unsigned float_mode) {
    recomp_context ctx{};ctx.r29=stack;ctx.r4=x;ctx.r5=y;
    ctx.mips3_float_mode=float_mode;ctx.f_odd=float_mode?&ctx.f1.u32l:&ctx.f0.u32h;
    for(unsigned i=16;i<=23;++i)(&ctx.r0)[i]=0x12345000+i;
    ctx.r30=0x56789abc;ctx.r31=0x80001000;
    func_global_asm_80700AE4(rdram,&ctx);
    check(ctx.r29==stack&&ctx.r30==0x56789abc&&ctx.r31==0x80001000,"depth query changed the caller stack or saved registers");
    for(unsigned i=16;i<=23;++i)check((&ctx.r0)[i]==0x12345000+i,"depth query changed a saved register");
    check(ctx.f_odd==(float_mode?&ctx.f1.u32l:&ctx.f0.u32h),"depth query changed the FPR alias");
    return uint32_t(ctx.r2);
}
}
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }
unsigned probePauseGeneration() { return 0; }
void vita_log(const char *,...) {}
namespace ultramodern::renderer {
std::vector<uint8_t> read_depthbuffer(uint32_t address,uint32_t size) {
    check(size==2,"game depth query copied more than its requested pixel");
    ++reads;requested=address;
    if(short_read)return {0xaa};
    std::vector<uint8_t> bytes;sink->readDepthFramebuffer(address,size,bytes);return bytes;
}
}

int main() {
    try {
        auto platform=createProbeEGL(".");unsigned queries=0;
        for(bool merge:{false,true}) {
            batching=merge;auto renderer=platform->createSink();sink=renderer.get();
            std::vector<uint8_t> memory(recomp::mem_size);auto *rdram=memory.data();sink->setRDRAM(rdram,memory.size());
            constexpr uint32_t depth=0x100000,color=0x200000;
            for(uint32_t alias:{0x80100000U,0xa0100000U})for(unsigned float_mode:{0U,1U}) {
                setup(rdram,alias);
                std::fill(memory.begin()+depth,memory.begin()+depth+32*24*2,0xa5);
                RT64::Tests::Depth::clear(*sink,depth,32,24,{0,0,32,24});
                const unsigned before=reads;
                check(query(rdram,7,9,float_mode)==32767,"original depth query did not see the rendered far plane");
                check(reads==before+1&&requested==depth+(9*32+7)*2,"depth query requested the wrong renderer pixel");
                check(MEM_HU((9*32+7)*2,alias)==0xfffc,"depth query did not restore packed N64 Z bytes");
                check(MEM_HU((9*32+6)*2,alias)==0xa5a5&&MEM_HU((9*32+8)*2,alias)==0xa5a5,"depth query overwrote neighboring RDRAM");
                RT64::Tests::Depth::rectangle(*sink,color,depth,32,24,{3,5,18,20},0.5f);
                check(query(rdram,7,9,float_mode)==16384,"depth query retained stale GPU depth after a draw");
                check(query(rdram,30,21,float_mode)==32767,"depth query changed an untouched pixel");
                RT64::Tests::Depth::rectangle(*sink,color,depth,32,24,{3,5,18,20},0.75f);
                check(query(rdram,7,9,float_mode)==16384,"occluded geometry replaced the query depth");
                RT64::Tests::Depth::rectangle(*sink,color,depth,32,24,{3,5,18,20},0.25f);
                check(query(rdram,7,9,float_mode)==8192,"nearer geometry did not replace the query depth");
                const unsigned clipped=reads;
                for(const auto xy:std::array<std::array<int,2>,4>{{{-1,9},{32,9},{7,-1},{7,24}}})
                    check(query(rdram,xy[0],xy[1],float_mode)==0xffff,"original viewport rejection changed");
                check(reads==clipped,"rejected screen coordinates still read the renderer");
                MEM_W(0,0xffffffff8076a060ULL)=int32_t(0xa0900000U);
                MEM_H((9*32+7)*2,0xffffffffa0900000ULL)=0x1000;
                check(query(rdram,7,9,float_mode)==8192,"unrendered depth no longer uses existing RAM");
                short_read=true;bool rejected=false;
                try { query(rdram,7,9,float_mode); }catch(const std::runtime_error &) { rejected=true; }
                check(rejected&&MEM_HU((9*32+7)*2,0xffffffffa0900000ULL)==0x1000,"short depth response changed RAM");
                short_read=false;queries+=10;
            }
        }
        std::printf("DK64 depth query: %u retained-routine cases, first GPU reads, occlusion, KSEG aliases, viewport rejection and RAM fallback passed\n",queries);
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
