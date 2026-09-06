#if DK64_VITA_PROBE_MAP > 0
#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "log.h"
#include <atomic>
#include <bit>

extern "C" void func_global_asm_805FF378(uint8_t *,recomp_context *);
extern "C" void func_global_asm_80712774(uint8_t *,recomp_context *);

namespace {
struct MapProbe {
    bool requested=false,entered=false,shot_this_round=false;
    uint32_t actor=0,input=0,next_shot=0,fire_until=0;
    unsigned firing_slot=0;
    int phase=0,control=0,remaining=0,total=0,timer=0,selection=0,ammo=0;
};
// A single packet keeps the button and both axes from different frames from
// being mixed when the controller callback runs on another runtime thread.
std::atomic<uint32_t> published_input{0};

bool probe_pointer(gpr address,uint32_t size) {
    const uint32_t value=uint32_t(address),physical=value&0x1fffffff;
    return value>=0x80000000 && value<0xc0000000
        && physical<recomp::mem_size && size<=recomp::mem_size-physical;
}

void update_map_probe(MapProbe &probe,uint8_t *rdram,recomp_context *ctx,int destination) {
    probe.input=0;
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
        // Snide's original entry supplies a practice-mode return route without
        // requiring a saved bonus-barrel exit from the surrounding level.
        if(destination==101 || destination==141 || destination==142 || destination==143)
            func_global_asm_80712774(rdram,&call);
        else func_global_asm_805FF378(rdram,&call);
        vita_log("Map probe requested map=%d exit=0 from=%d; next_map=%d next_exit=%d pending=%u mode=%u",
            destination,map,MEM_W(0,0xffffffff807444e4ULL),MEM_W(0,0xffffffff807444e8ULL),
            unsigned(MEM_BU(0,0xffffffff8076a0b1ULL)&1),unsigned(MEM_BU(0,0xffffffff80755318ULL)));
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
        const int remaining=MEM_H(0x14,info),total=MEM_H(0x16,info),timer=MEM_H(0,info);
        const int selection=MEM_BU(0x22,aad),ammo=MEM_BU(0x23,aad);
        if(probe.actor!=uint32_t(actor) || probe.phase!=phase || probe.control!=control
            || probe.remaining!=remaining || probe.total!=total || probe.selection!=selection
            || probe.ammo!=ammo || (probe.timer<0 && timer>=0)) {
            vita_log("Klamour map=%d actor=%08x phase=%d control=%d timer=%d lag=%d remaining=%d total=%d selection=%d ammo=%d targets=%02x,%02x,%02x,%02x,%02x,%02x",
                map,uint32_t(actor),phase,control,timer,MEM_W(0,0xffffffff80744478ULL),remaining,total,selection,ammo,
                unsigned(MEM_BU(8,info)),unsigned(MEM_BU(9,info)),unsigned(MEM_BU(10,info)),
                unsigned(MEM_BU(11,info)),unsigned(MEM_BU(12,info)),unsigned(MEM_BU(13,info)));
        }
        if(phase>0 && control==0 && !MEM_BU(0,0xffffffff807444ecULL)
            && !(MEM_W(0,0xffffffff807fbb60ULL)&2)) {
            const uint32_t frame=MEM_W(0,0xffffffff8076a064ULL);
            if(probe.actor!=uint32_t(actor) || probe.phase<=0 || probe.control!=0) {
                probe.next_shot=frame;probe.fire_until=frame;probe.shot_this_round=false;
            }
            if(timer>probe.timer) probe.shot_this_round=false;
            // Child 5 is the banana. Its coordinate-table slot is stored in
            // gameinfo->unk8[5] (low seven bits), plus one for the neutral slot.
            // Centering the stick and pressing A reloads the original gun.
            unsigned slot=ammo?((MEM_BU(13,info)&0x7f)+1):0;
            if(slot<=6) {
                const bool visible=timer>20 && std::bit_cast<float>(uint32_t(MEM_W(4,info)))>=0.2f;
                if(int32_t(frame-probe.next_shot)>=0
                    && (!ammo || (!probe.shot_this_round && visible && !MEM_BU(0x18,aad)))) {
                    // One projectile per appearance avoids shooting into a
                    // different target after the next shuffle. Leave a release
                    // frame between A pulses, including a neutral-stick reload.
                    probe.next_shot=frame+3;probe.fire_until=frame+2;probe.firing_slot=slot;
                    if(ammo) probe.shot_this_round=true;
                }
                const bool fire=int32_t(frame-probe.fire_until)<0;
                if(fire) slot=probe.firing_slot;
                // Axes use two-bit codes: zero, negative, positive. Bit 4 marks
                // an active override; buttons occupy the upper halfword.
                constexpr unsigned xs[]={0,1,0,2,1,2,0},ys[]={0,2,2,2,1,1,1};
                probe.input=0x10 | xs[slot] | (ys[slot]<<2) | (fire?0x80000000U:0);
            }
        }
        probe.actor=uint32_t(actor);probe.phase=phase;probe.control=control;
        probe.remaining=remaining;probe.total=total;probe.timer=timer;probe.selection=selection;probe.ammo=ammo;
        break;
    }
}
}

extern "C" void dk64_vita_map_probe(uint8_t *rdram,recomp_context *ctx) {
    static MapProbe probe;
    update_map_probe(probe,rdram,ctx,DK64_VITA_PROBE_MAP);
    published_input.store(probe.input,std::memory_order_release);
}

extern "C" bool dk64_vita_map_probe_input(uint16_t *buttons,float *x,float *y) {
    const uint32_t packet=published_input.load(std::memory_order_acquire);
    if(!(packet&0x10)) return false;
    *buttons=packet>>16;
    auto axis=[](unsigned code) { return code==1?-0.6f:code==2?0.6f:0.0f; };
    *x=axis(packet&3);*y=axis((packet>>2)&3);
    return true;
}
#endif
