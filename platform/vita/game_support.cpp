// DK64 integration hooks from src/game/recomp_api.cpp, without frontend dependencies.
#include "recomp.h"
#include "librecomp/game.hpp"
#include "librecomp/addresses.hpp"
#include "librecomp/overlays.hpp"
#include <stdexcept>
#include "log.h"

extern "C" void yield_self(uint8_t *rdram);
extern "C" void dk64_vita_frame_wait(uint8_t *rdram) {
#if DK64_VITA_DIAGNOSTICS
    static bool logged=false;
    if(!logged) { vita_log("DK64 cooperative frame wait reached"); logged=true; }
#endif
    yield_self(rdram);
}

extern "C" void load_dk64_overlay(uint32_t compressed_rom,int32_t ram_addr,uint32_t size) {
    struct Overlay { uint32_t compressed,uncompressed,size; };
    constexpr Overlay overlays[]={{0x113f0,0x2000000,0x165d50},{0xcbe70,0x2165d50,0xff10},
        {0xd4b00,0x2175c60,0x3100},{0xd6b00,0x2178d60,0x4e10},{0xd9a40,0x217db70,0x9ef0},
        {0xdf600,0x2187a60,0xc160},{0xe6780,0x2193bc0,0x61b0},{0xea0b0,0x2199d70,0x12dc0},
        {0xf41a0,0x21acb30,0x26c00},{0xfd2f0,0x21d3730,0xac30}};
    for(const auto &overlay:overlays) if(overlay.compressed==compressed_rom) {
        load_overlays(overlay.uncompressed,compressed_rom==0x113f0?int32_t(0x805fb300):int32_t(0x80024000),overlay.size);
        vita_log("Mapped DK64 code overlay at ROM 0x%08x",compressed_rom);
        return;
    }
    // This hook also runs for asset loads. Only the ten code overlays need
    // native function-table mapping; other ROM transfers proceed normally.
}
extern "C" void boot_osPiRawStartDma(uint8_t *rdram,recomp_context *ctx) {
    if(ctx->r4!=0) throw std::runtime_error("Unexpected boot ROM DMA write");
    recomp::do_rom_read(rdram,ctx->r6,uint32_t(ctx->r5)+recomp::rom_base,uint32_t(ctx->r7));
    ctx->r2=0;
}
// DK64's boot loader calls raw PI DMA directly. Its desktop patches route this
// through the same synchronous helper; keep that game-specific behavior here.
extern "C" void __wrap_osPiRawStartDma_recomp(uint8_t *rdram,recomp_context *ctx) {
    boot_osPiRawStartDma(rdram,ctx);
}
extern "C" void osPiReadIo_recomp(uint8_t *rdram,recomp_context *ctx) {
    recomp::do_rom_pio(rdram,ctx->r5,(uint32_t(ctx->r4)|recomp::rom_base)&0x1fffffff);
    ctx->r2=0;
}
extern "C" void osPfsInit_recomp(uint8_t *,recomp_context *ctx) { ctx->r2=11; }
extern "C" void __f_to_ull_recomp(uint8_t *,recomp_context *ctx) {
    const uint64_t value=uint64_t(ctx->f12.fl);
    ctx->r2=int32_t(value>>32); ctx->r3=int32_t(value);
}
extern "C" void __osSpSetStatus_recomp(uint8_t *,recomp_context *) {
    // SP completion is managed by N64ModernRuntime's task queue.
}
extern "C" void osViGetCurrentMode_recomp(uint8_t *rdram,recomp_context *ctx) {
    const gpr current=gpr(int32_t(MEM_W(0,0xffffffff80010190ULL)));
    const gpr mode=current?gpr(int32_t(MEM_W(8,current))):0;
    ctx->r2=mode?MEM_BU(3,mode):0;
}
