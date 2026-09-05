#include "recomp.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_806FFF88(uint8_t *,recomp_context *);
static unsigned syncs=0,allocations=0,fallbacks=0;
static uint32_t expected_source=0;
static constexpr gpr output=0xffffffff80500000ULL;
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
static uint16_t pixel(unsigned x,unsigned y) { return uint16_t((((x*3+y*5+(expected_source>>12))&0x7fff)<<1)|1); }
extern "C" void _malloc(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==0xa000,"photo allocation size changed"); ++allocations; ctx->r2=output;
}
extern "C" void func_global_asm_806FFF5C(uint8_t *,recomp_context *ctx) { ++fallbacks; ctx->r2=0x1234; }
extern "C" void func_global_asm_806FFC04(uint8_t *,recomp_context *ctx) {
    // Isolate capture synchronization/layout from the game's color conversion.
    ctx->r2=ctx->r4;
}
extern "C" void dk64_vita_sync_framebuffer(uint8_t *rdram,uint32_t source,uint32_t width,uint32_t height) {
    check(source==expected_source && width==320 && height==240,"photo synchronized the wrong framebuffer range");
    ++syncs;
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) MEM_H((y*width+x)*2,source)=pixel(x,y);
}
int main() {
    try {
        std::vector<uint8_t> memory(8*1024*1024); auto *rdram=memory.data();
        recomp_context ctx{}; ctx.r29=0xffffffff80600000ULL;
        constexpr gpr actor=0xffffffff80401000ULL;
        MEM_W(0,0xffffffff807fbb40ULL)=static_cast<int32_t>(actor);
        MEM_W(0x58,actor)=217;
        func_global_asm_806FFF88(rdram,&ctx);
        check(fallbacks==1 && allocations==0 && syncs==0 && ctx.r2==0x1234,"special-photo fallback performed a framebuffer capture");
        MEM_W(0x58,actor)=1;
        MEM_H(0,0xffffffff80744490ULL)=320; MEM_H(0,0xffffffff80744494ULL)=240;
        for(unsigned buffer=0;buffer<2;++buffer) {
            expected_source=0x80300000+buffer*0x40000;
            MEM_W(buffer*4,0xffffffff80744470ULL)=expected_source;
            MEM_B(0,0xffffffff807444fcULL)=buffer;
            func_global_asm_806FFF88(rdram,&ctx);
            check(syncs==buffer+1,"fairy photograph did not request completed framebuffer data");
            check(ctx.r2==output && ctx.r29==0xffffffff80600000ULL,"capture changed the result or guest stack");
            for(unsigned tile_y=0;tile_y<2;++tile_y) for(unsigned tile_x=0;tile_x<5;++tile_x)
                for(unsigned y=0;y<64;++y) for(unsigned x=0;x<32;++x) {
                    const unsigned index=((tile_y*5+tile_x)*64+y)*32+x;
                    check(MEM_HU(index*2,output)==pixel(80+tile_x*32+x,56+tile_y*64+y),"photo tile read stale or incorrectly addressed framebuffer pixels");
                }
        }
        check(allocations==2,"capture changed original photo allocation ownership");
        std::puts("Photo capture: source selection, synchronized center tiles, allocation, fallback and guest stack passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
