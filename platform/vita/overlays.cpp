#include "recomp.h"

extern "C" void dk64_vita_cutscene_fade(uint8_t *rdram,recomp_context *ctx) {
    gpr cursor=ctx->r4;
    const uint32_t alpha=uint8_t(ctx->r5);
    const auto emit=[&](uint32_t w0,uint32_t w1) {
        do_sw(rdram,0,cursor,w0); do_sw(rdram,4,cursor,w1);
        cursor=ADD32(cursor,8);
    };
    // Match upstream 80703AB0's classic F3DEX2 commands. Its uniform primitive
    // replaces the original quad's alternating alpha and doubled-alpha corners.
    // Keep the caller's existing scissor, including any narrowed viewport.
    if(alpha) {
        emit(0xd9000000,0);          // Clear geometry mode.
        emit(0xd9ffffff,0x00200004); // Shade and smooth shading.
        emit(0xd7000000,0xffffffff); // Texture off.
        emit(0xe7000000,0);          // Pipe sync.
        emit(0xe200001c,0x00504340); // Cloud surface render mode.
        emit(0xfa000000,alpha);      // Black primitive, supplied alpha.
        emit(0xe3000a01,0);          // One-cycle mode.
        emit(0xfcffffff,0xfffdf6fb); // Primitive color and alpha.
        const auto coordinate=[&](gpr address) { return (uint32_t(MEM_H(0,address))<<2)&0xfff; };
        emit(0xf6000000|(coordinate(0xffffffff807444a0ULL)<<12)|coordinate(0xffffffff807444a4ULL),
            (coordinate(0xffffffff80744498ULL)<<12)|coordinate(0xffffffff8074449cULL));
    }
    emit(0xe7000000,0);
    ctx->r2=cursor;
}
