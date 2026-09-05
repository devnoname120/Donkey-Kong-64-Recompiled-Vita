// Optional integration-test input at the game's controller callback. It does
// not simulate Vita3K desktop keys or validate physical controller delivery.
#pragma once
#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include "log.h"
#include <bit>
#include <chrono>

class AdventureProbe {
    bool started=false;
    std::chrono::steady_clock::time_point first_input;
    uint16_t previous=0;
    int32_t previous_map=-1,previous_mode=-1;
    bool previous_paused=false;
    int64_t playable_ms=-1;
public:
    void poll(uint8_t *rdram,uint16_t *buttons,float *x,float *y,bool pause_test=false) {
        *buttons=0; *x=0; *y=0;
        // Start relative to the first controller poll, not ROM/tool startup.
        if (!started) { first_input=std::chrono::steady_clock::now(); started=true; }
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-first_input).count();
        const int32_t map=rdram?MEM_W(0,0xffffffff8076a0a8ULL):-1;
        const int32_t mode=rdram?MEM_BU(0,0xffffffff80755318ULL):-1;
        const int32_t mode_copy=rdram?MEM_BU(0,0xffffffff80755314ULL):-1;
        const bool adventure=mode==6||mode_copy==6;
        const bool paused=rdram && (MEM_W(0,0xffffffff807fbb60ULL)&2);
        const bool pulse=ms>=1000 && (ms-1000)%4000<1000;
        if(pulse) *buttons=adventure||mode==5?0x8000:0x1000;
        if(adventure && rdram && !MEM_BU(0,0xffffffff807444ecULL)) {
            if(playable_ms<0 && !MEM_BU(0,0xffffffff807463b8ULL)) playable_ms=ms;
            // Change direction periodically so a wall does not consume the
            // remainder of the movement probe. This is diagnostic input.
            const unsigned direction=(ms/6000)%4;
            if(direction<2) *x=direction==0?0.6f:-0.6f;
            else *y=direction==2?0.6f:-0.6f;
        }
        if(pause_test && playable_ms>=0) {
            const auto elapsed=ms-playable_ms;
            if(elapsed>=8000 && elapsed<22000) {
                *buttons=((elapsed<10000)||(elapsed>=20000))?0x1000:0;
                *x=0; *y=0;
            }
        }
        if(*buttons!=previous || map!=previous_map || mode!=previous_mode || paused!=previous_paused) {
            const int32_t player=rdram?MEM_W(0,0xffffffff807fbb4cULL):0;
            const bool player_valid=player && (uint32_t(player)&0x1fffffff)<recomp::mem_size-0x88;
            auto position=[&](unsigned offset) { return player_valid?std::bit_cast<float>(uint32_t(MEM_W(offset,player))):0.0f; };
            vita_log("Scripted input elapsed_ms=%lld map=%d mode=%d/%d buttons=%04x stick=%.1f,%.1f lag=%d cutscene=%u auto=%u pause=%u player=%08x pos=%.2f,%.2f,%.2f",
                static_cast<long long>(ms),map,mode,mode_copy,*buttons,*x,*y,rdram?MEM_W(0,0xffffffff80744478ULL):-1,
                rdram?MEM_BU(0,0xffffffff807444ecULL):0,rdram?MEM_BU(0,0xffffffff807463b8ULL):0,
                unsigned(paused),uint32_t(player),position(0x7c),position(0x80),position(0x84));
            if(adventure && rdram) {
                const uint64_t story_start=(uint64_t(uint32_t(MEM_W(0,0xffffffff807f5ce0ULL)))<<32)|uint32_t(MEM_W(0,0xffffffff807f5ce4ULL));
                const unsigned story_index=MEM_BU(0,0xffffffff807f5d14ULL);
                vita_log("Story index=%u scene=%d timer=%u flags=%04x elapsed_cs=%llu deadline_cs=%u",
                    story_index,MEM_H(0,0xffffffff807476f4ULL),MEM_HU(0,0xffffffff807476f0ULL),MEM_HU(0,0xffffffff807f5cf4ULL),
                    static_cast<unsigned long long>(story_start?(osGetTime()-story_start)*64/30000000:0),
                    story_index<6?uint32_t(MEM_W(story_index*8,0xffffffff80747708ULL)):0);
            }
        }
        previous=*buttons; previous_map=map; previous_mode=mode; previous_paused=paused;
    }
};
