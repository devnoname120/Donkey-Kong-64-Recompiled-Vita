// Gameplay corrections from upstream patches, without desktop UI/interpolation.
#include "recomp.h"
#include <bit>

extern "C" void isFlagSet(uint8_t *,recomp_context *);
extern "C" void setFlag(uint8_t *,recomp_context *);
extern "C" void deleteActor(uint8_t *,recomp_context *);
extern "C" void func_global_asm_80703374(uint8_t *,recomp_context *);

extern "C" int dk64_vita_cover_stopped_transition(uint8_t *rdram,recomp_context *ctx) {
    const float speed=std::bit_cast<float>(uint32_t(MEM_W(0,0xffffffff807fd88cULL)));
    const float progress=std::bit_cast<float>(uint32_t(MEM_W(0,0xffffffff807fd888ULL)));
    if(speed!=0.0f || !(progress>28.0f)) return 0;
    // This hook runs before the original prologue. Reserve our own outgoing
    // argument area for the fifth (alpha) argument instead of touching the
    // caller's locals. Only the returned display-list pointer is observable.
    recomp_context call=*ctx;
    call.r29=ADD32(call.r29,-0x20);
    call.r5=call.r6=call.r7=0;
    do_sw(rdram,0x10,call.r29,255);
    func_global_asm_80703374(rdram,&call);
    ctx->r2=call.r2;
    return 1;
}

extern "C" void dk64_vita_cover_transition_end(uint8_t *rdram,recomp_context *ctx) {
    // 80704484 keeps its display-list cursor at sp+0x60. Upstream's
    // "MJ Cutscene flash" fix covers the frame that crosses progress 31,
    // before relying on VI blanking or the following stopped-state call.
    recomp_context call=*ctx;
    call.f_odd=call.mips3_float_mode?&call.f1.u32l:&call.f0.u32h;
    call.r4=MEM_W(0x60,ctx->r29);
    call.r29=ADD32(call.r29,-0x20);
    call.r5=call.r6=call.r7=0;
    do_sw(rdram,0x10,call.r29,255);
    func_global_asm_80703374(rdram,&call);
    do_sw(rdram,0x60,ctx->r29,call.r2);
}

extern "C" void dk64_vita_restore_helm_medals(uint8_t *rdram,recomp_context *ctx,uint32_t existing_file) {
    if(!existing_file) return;
    // The file has been loaded and temporary flags cleared at this hook. Use
    // the game's flag accessors with its guest stack, preserving caller registers.
    recomp_context call=*ctx;
    call.r4=0x302; call.r5=0; // Blast-o-Matic shut down, permanent flag.
    isFlagSet(rdram,&call);
    if(!call.r2) return;
    for(unsigned flag=0x4b;flag<0x50;++flag) {
        call.r4=flag; call.r5=1; call.r6=2; // Temporary completion flags.
        setFlag(rdram,&call);
    }
}

extern "C" void dk64_vita_remove_cutscene_controllers(uint8_t *rdram,recomp_context *ctx) {
    // GlobalASMStruct53 is two N64 words; Actor::unk58 is a 32-bit actor type.
    // Match playCutscene's upstream cleanup immediately before spawning a new
    // controller, after the existing-cutscene rejection guard has passed.
    recomp_context call=*ctx;
    for(unsigned i=0;i<MEM_HU(0,0xffffffff807fbb34ULL);) {
        const gpr actor=MEM_W(i*8,0xffffffff807fb930ULL);
        if(MEM_W(0x58,actor)==173) {
            call.r4=actor;
            deleteActor(rdram,&call);
            // Deletion is deferred during actor iteration, but otherwise moves
            // the last entry into this slot. Inspect that entry before advancing.
            if(i<MEM_HU(0,0xffffffff807fbb34ULL) && gpr(MEM_W(i*8,0xffffffff807fb930ULL))!=actor) continue;
        }
        ++i;
    }
}
