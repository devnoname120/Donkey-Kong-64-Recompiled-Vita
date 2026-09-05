#include "recomp.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_80611730(uint8_t *,recomp_context *);
static std::vector<gpr> freed;
extern "C" void func_global_asm_80611724(uint8_t *rdram,recomp_context *ctx) {
    // The original seed helper only stores its arguments in the caller's home area.
    MEM_W(0,ctx->r29)=ctx->r4; MEM_W(4,ctx->r29)=ctx->r5;
}
extern "C" void func_global_asm_80611408(uint8_t *,recomp_context *ctx) { freed.push_back(ctx->r4); }
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        std::vector<uint8_t> memory(8*1024*1024); auto *rdram=memory.data();
        recomp_context ctx{}; ctx.r29=0xffffffff80400000ULL;
        constexpr gpr count=0xffffffff807f5a58ULL,entries=0xffffffff807f0a58ULL;
        // Deliberately mismatch the uncached word removed by the desktop patch.
        MEM_W(0,0xffffffffa00002e8ULL)=0;
        MEM_W(0,count)=2;
        MEM_W(0,entries)=0x80401000; MEM_B(4,entries)=1;
        MEM_W(8,entries)=0x80402000; MEM_B(12,entries)=2;
        func_global_asm_80611730(rdram,&ctx);
        check(freed==std::vector<gpr>{0xffffffff80401000ULL},"uncached check suppressed a due deferred free");
        check(MEM_W(0,count)==1 && uint32_t(MEM_W(0,entries))==0x80402000 && MEM_BU(4,entries)==1,"deferred-free compaction/countdown changed");
        check(ctx.r29==0xffffffff80400000ULL,"deferred-free function did not restore the guest stack");
        func_global_asm_80611730(rdram,&ctx);
        check(freed==std::vector<gpr>({0xffffffff80401000ULL,0xffffffff80402000ULL}) && MEM_W(0,count)==0,"remaining deferred free did not complete");
        func_global_asm_80611730(rdram,&ctx);
        check(freed.size()==2,"empty deferred-free queue freed another allocation");
        std::puts("Deferred frees: removed memory check, countdown, compaction, empty queue and guest stack passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
