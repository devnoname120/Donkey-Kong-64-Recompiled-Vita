#include "recomp.h"

extern "C" void dk64_vita_frame_wait(uint8_t *rdram);
extern "C" uint64_t osGetTime();
extern "C" void playSound(uint8_t *rdram,recomp_context *ctx);

extern "C" void dk64_vita_present_boot_logo(uint8_t *rdram,recomp_context *ctx) {
    constexpr gpr logo_mode=0xffffffff80744510ULL;
    // The original VI callback unblanks the CPU logo after 60 retraces. Fast
    // native initialization can reach the main loop sooner and reset this mode
    // before the logo is ever visible. Yield to that callback before continuing.
    while(MEM_BU(0,logo_mode)==1) dk64_vita_frame_wait(rdram);

    // Move the original "OK" sound from early initialization to this handoff.
    // Keep its outgoing arguments and floating-register aliases private.
    recomp_context call=*ctx;
    call.f_odd=call.mips3_float_mode?&call.f1.u32l:&call.f0.u32h;
    call.r29=ADD32(call.r29,-0x20);
    call.r4=0x23c; call.r5=0x7fff; call.r6=0x427c0000; call.r7=0x3f800000;
    do_sw(rdram,0x10,call.r29,0); do_sw(rdram,0x14,call.r29,0);
    playSound(rdram,&call);

    // Match upstream's 1.4-second logo hold using the runtime's N64 clock.
    // No extra delay is needed if this boot path has no CPU logo to present.
    if(MEM_BU(0,logo_mode)==2) {
        constexpr uint64_t hold_cycles=65625000; // 1.4 * 46,875,000 Hz.
        const uint64_t started=osGetTime();
        while(MEM_BU(0,logo_mode)==2 && uint64_t(osGetTime()-started)<hold_cycles)
            dk64_vita_frame_wait(rdram);
    }
}
