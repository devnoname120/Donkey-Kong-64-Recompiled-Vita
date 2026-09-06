#if DK64_VITA_PROBE_MAP > 0
#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "log.h"
#include <bit>

extern "C" void func_global_asm_805FF378(uint8_t *,recomp_context *);

namespace {
struct MapProbe {
    bool requested=false,entered=false;
    uint32_t actor=0;
    int phase=0,control=0,remaining=0,timer=0,selection=0;
};

bool probe_pointer(gpr address,uint32_t size) {
    const uint32_t value=uint32_t(address),physical=value&0x1fffffff;
    return value>=0x80000000 && value<0xc0000000
        && physical<recomp::mem_size && size<=recomp::mem_size-physical;
}

void update_map_probe(MapProbe &probe,uint8_t *rdram,recomp_context *ctx,int destination) {
    const int map=MEM_W(0,0xffffffff8076a0a8ULL);
    if(!probe.requested) {
        // Called by the game's main-loop timing entry, after its timing update.
        // Request a transition from the initial playable area only, after the
        // Adventure mode handoff, cutscene, automatic movement and fade finish.
        if((map!=176 && map!=171) || MEM_BU(0,0xffffffff80755318ULL)!=6
            || MEM_BU(0,0xffffffff80755314ULL)!=6 || MEM_BU(0,0xffffffff807444ecULL)
            || MEM_BU(0,0xffffffff807463b8ULL) || (MEM_W(0,0xffffffff807fbb60ULL)&2)
            || (MEM_BU(0,0xffffffff8076a0b1ULL)&1)
            || std::bit_cast<float>(uint32_t(MEM_W(0,0xffffffff807fd88cULL)))!=0.0f) return;
        probe.requested=true;
        recomp_context call=*ctx;
        call.f_odd=call.mips3_float_mode?&call.f1.u32l:&call.f0.u32h;
        call.r29=ADD32(call.r29,-0x20);
        call.r4=destination; call.r5=0;
        func_global_asm_805FF378(rdram,&call);
        vita_log("Map probe requested map=%d exit=0 from=%d; next_map=%d next_exit=%d pending=%u",
            destination,map,MEM_W(0,0xffffffff807444e4ULL),MEM_W(0,0xffffffff807444e8ULL),
            unsigned(MEM_BU(0,0xffffffff8076a0b1ULL)&1));
    }
    if(map!=destination) return;
    if(!probe.entered) {
        probe.entered=true;
        vita_log("Map probe entered map=%d",map);
    }
    if(map!=101 && map!=141 && map!=142 && map!=143) return;

    // Observe Klamour's controller without writing its timer, targets or score.
    // The actor table ends before the adjacent count/actor-pointer records.
    const unsigned count=MEM_HU(0,0xffffffff807fbb34ULL);
    if(count>64) return;
    for(unsigned i=0;i<count;++i) {
        const gpr actor=MEM_W(i*8,0xffffffff807fb930ULL);
        // ACTOR_BARRELGUN_KRAZYKONGKLAMOUR is 125 (124 is Peril Path Panic).
        if(!probe_pointer(actor,0x17c) || MEM_W(0x58,actor)!=125) continue;
        const gpr aad=MEM_W(0x174,actor),info=MEM_W(0x178,actor);
        if(!probe_pointer(aad,0x28) || !probe_pointer(info,0x18)) continue;
        const int phase=MEM_H(0,aad),control=MEM_BU(0x154,actor);
        const int remaining=MEM_H(0x14,info),timer=MEM_H(0,info),selection=MEM_BU(0x22,aad);
        if(probe.actor!=uint32_t(actor) || probe.phase!=phase || probe.control!=control
            || probe.remaining!=remaining || probe.selection!=selection || (probe.timer<0 && timer>=0)) {
            vita_log("Klamour map=%d actor=%08x phase=%d control=%d timer=%d lag=%d remaining=%d total=%d selection=%d targets=%02x,%02x,%02x,%02x,%02x,%02x",
                map,uint32_t(actor),phase,control,timer,MEM_W(0,0xffffffff80744478ULL),remaining,MEM_H(0x16,info),selection,
                unsigned(MEM_BU(8,info)),unsigned(MEM_BU(9,info)),unsigned(MEM_BU(10,info)),
                unsigned(MEM_BU(11,info)),unsigned(MEM_BU(12,info)),unsigned(MEM_BU(13,info)));
        }
        probe.actor=uint32_t(actor);probe.phase=phase;probe.control=control;
        probe.remaining=remaining;probe.timer=timer;probe.selection=selection;
        break;
    }
}
}

extern "C" void dk64_vita_map_probe(uint8_t *rdram,recomp_context *ctx) {
    static MapProbe probe;
    update_map_probe(probe,rdram,ctx,DK64_VITA_PROBE_MAP);
}
#endif
