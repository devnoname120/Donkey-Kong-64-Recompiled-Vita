// Exercise the original generated producer/drain/ownership routines. DMA and
// decompression are modeled here; this is not a scheduler or codec benchmark.
#include "recomp.h"
#include <cstdio>
#include <deque>
#include <map>
#include <stdexcept>
#include <vector>

extern "C" void getPointerTableFile(uint8_t *,recomp_context *);
extern "C" void func_global_asm_8066AF40(uint8_t *,recomp_context *);
static constexpr gpr count_addr=0xffffffff807f9680ULL, jobs=0xffffffff807fa8a0ULL;
static constexpr gpr io_records=0xffffffff807f9688ULL, queue=0xffffffff807656d0ULL;
static constexpr gpr workspace=0xffffffff80748e14ULL, size_tables=0xffffffff807fb1a0ULL;
static constexpr gpr size_table=0xffffffff80300000ULL, stack_top=0xffffffff80600000ULL;
static constexpr unsigned capacity=192;
static uint32_t next_allocation=0x80100000;
static std::map<uint32_t,unsigned> live;
static std::deque<unsigned> completions;
static unsigned dma_calls=0, decoded=0, released=0, received=0, file_lookups=0;
static gpr cached_pointer=0;
struct LimitReached {};
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
static uint8_t pattern(unsigned file,unsigned offset) { return uint8_t(file^(offset*3)); }
static void load(uint8_t *rdram,uint32_t rom,gpr destination,unsigned size) {
    const unsigned file=(rom-0x100000)/0x100;
    for(unsigned i=0;i<size;++i) MEM_B(i,destination)=pattern(file,i);
}
extern "C" void _malloc(uint8_t *,recomp_context *ctx) {
    const unsigned size=ctx->r4; check(size>0 && size<=0x2400,"unexpected allocation size");
    const uint32_t address=next_allocation; next_allocation+=(size+15)&~15U;
    check(live.emplace(address,size).second,"allocator reused a live block");
    ctx->r2=gpr(int32_t(address));
}
extern "C" void _free(uint8_t *,recomp_context *ctx) {
    check(live.erase(uint32_t(ctx->r4))==1,"decompression freed an output or released a block twice"); ++released;
}
extern "C" void func_global_asm_8066B5F4(uint8_t *,recomp_context *) {}
extern "C" void func_global_asm_8066B4AC(uint8_t *,recomp_context *) {}
extern "C" void func_global_asm_8066B8C8(uint8_t *,recomp_context *) {}
extern "C" void osInvalDCache_recomp(uint8_t *,recomp_context *) {}
extern "C" void func_global_asm_8066B5C8(uint8_t *,recomp_context *ctx) { ctx->r2=cached_pointer; }
extern "C" void func_global_asm_806111BC(uint8_t *,recomp_context *) { throw std::runtime_error("unexpected alternate arena"); }
extern "C" void func_global_asm_8066B4D4(uint8_t *rdram,recomp_context *ctx) {
    ++file_lookups;
    MEM_W(0,ctx->r6)=0x100000+uint32_t(ctx->r5)*0x100;
    MEM_W(0,ctx->r7)=ctx->r4==1?16:32;
}
extern "C" void func_global_asm_8060B140(uint8_t *rdram,recomp_context *ctx) {
    load(rdram,ctx->r4,ctx->r5,MEM_W(0,ctx->r6));
}
extern "C" void osPiStartDma_recomp(uint8_t *rdram,recomp_context *ctx) {
    const unsigned index=MEM_W(0,count_addr);
    check(index<capacity && ctx->r4==io_records+index*24,"DMA record stride or capacity mismatch");
    check(ctx->r6==0 && gpr(MEM_W(0x18,ctx->r29))==queue,"wrong DMA direction/completion queue");
    const gpr destination=MEM_W(0x10,ctx->r29); const unsigned size=MEM_W(0x14,ctx->r29);
    load(rdram,ctx->r7,destination,size);
    check(completions.size()<capacity,"completion queue overflow");
    completions.push_back(index); ++dma_calls; ctx->r2=0;
}
extern "C" void osRecvMesg_recomp(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==queue && ctx->r6==1 && ctx->r5==0,"drain did not await the expected completion");
    check(!completions.empty(),"drain waited for a nonexistent DMA completion");
    completions.pop_front(); ++received; ctx->r2=0;
}
extern "C" void func_dk64_boot_800024E0(uint8_t *rdram,recomp_context *ctx) {
    const gpr source=MEM_W(0,ctx->r4),destination=MEM_W(0,ctx->r5);
    check(live.at(uint32_t(source))==16 && live.at(uint32_t(destination))==64,"compressed buffer ownership/size mismatch");
    check(live.at(uint32_t(ctx->r6))==0x2400,"decompressor workspace mismatch");
    const unsigned file=MEM_BU(0,source);
    for(unsigned i=0;i<64;++i) MEM_B(i,destination)=pattern(file,i);
    ++decoded;
}
extern "C" void raiseException(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==6,"wrong limit exception"); throw LimitReached{};
}
static gpr request(uint8_t *rdram,unsigned table,gpr file,bool immediate,bool skip_cache=true) {
    recomp_context ctx{}; ctx.r29=stack_top; ctx.r4=table; ctx.r5=file; ctx.r6=immediate; ctx.r7=skip_cache;
    getPointerTableFile(rdram,&ctx);
    check(ctx.r29==stack_top,"asset loader did not restore its guest stack");
    return ctx.r2;
}
static void drain(uint8_t *rdram) {
    recomp_context ctx{}; ctx.r29=stack_top; func_global_asm_8066AF40(rdram,&ctx);
    check(ctx.r29==stack_top,"decompression drain did not restore its guest stack");
}
int main() {
    try {
        std::vector<uint8_t> memory(8*1024*1024); auto *rdram=memory.data();
        MEM_B(1,0xffffffff80748e18ULL)=1;
        MEM_W(4,size_tables)=int32_t(size_table);
        for(unsigned i=0;i<256;++i) MEM_W(i*4,size_table)=64;
        MEM_W(0,size_tables)=0x13579bdf; // Exactly after the final job record.
        std::vector<gpr> outputs;
        for(unsigned i=0;i<capacity;++i) outputs.push_back(request(rdram,i&1,i,false));
        check(MEM_W(0,count_addr)==capacity && dma_calls==capacity,"producer did not fill its complete original capacity");
        check(MEM_W(0,size_tables)==0x13579bdf,"last job overwrote the adjacent size table");
        drain(rdram);
        check(completions.empty() && received==capacity && decoded==capacity/2,"drain lost a completion or decompression job");
        check(MEM_W(0,count_addr)==0 && MEM_W(0,workspace)==0,"drain left stale count/workspace state");
        check(released==capacity/2+1 && live.size()==capacity,"compressed inputs/workspace were not released exactly once");
        for(unsigned i=0;i<capacity;++i) {
            const unsigned size=i&1?64:32; check(live.at(uint32_t(outputs[i]))==size,"returned output was released prematurely");
            for(unsigned byte=0;byte<size;++byte) check(MEM_BU(byte,outputs[i])==pattern(i,byte),"loaded output bytes changed");
        }
        const auto before=live.size(); const gpr immediate=request(rdram,1,7,true); drain(rdram);
        check(live.size()==before+1 && live.at(uint32_t(immediate))==64,"immediate compressed load changed ownership");
        const auto lookup_count=file_lookups;
        cached_pointer=0xffffffff80410000ULL;
        check(request(rdram,0,42,false,false)==cached_pointer,"cached asset pointer changed");
        check(request(rdram,0,0xffffffff80420000ULL,false,false)==0xffffffff80420000ULL,"direct KSEG0 asset pointer changed");
        check(file_lookups==lookup_count && completions.empty(),"cache hits performed a new transfer");
        cached_pointer=0;
        for(unsigned i=0;i<capacity;++i) request(rdram,0,i,false);
        bool rejected=false;
        try { request(rdram,0,capacity,false); } catch(const LimitReached&) { rejected=true; }
        check(rejected && MEM_W(0,count_addr)==capacity && completions.size()==capacity,"193rd deferred job was not rejected before submission");
        check(MEM_W(0,size_tables)==0x13579bdf,"overflow guard allowed an adjacent-table write");
        std::puts("Decompression: 192 mixed jobs, completion accounting, ownership, cache paths and overflow guard passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
