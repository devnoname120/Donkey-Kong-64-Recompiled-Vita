// Required DK64 timing behavior from patches_main.c and timing_fixes.c, using
// guest-memory accessors so the same implementation runs on 32/64-bit hosts.
#include "recomp.h"
#include <algorithm>
#include <cstdint>

extern "C" void dk64_vita_frame_wait(uint8_t *rdram);
namespace {
    constexpr gpr lag=0xffffffff80744478ULL,render_mode=0xffffffff807444ecULL;
    constexpr gpr map_address=0xffffffff8076a0a8ULL,object_timer=0xffffffff8076a064ULL;
    constexpr gpr frame_count=0xffffffff80767cc4ULL,last_frame=0xffffffff8076af10ULL;
    constexpr gpr history=0xffffffff8076af00ULL,history_index=0xffffffff80745290ULL;
    constexpr int japes=7,rap=76,helm_story=152,theatre=153,dk_house=171,rock_story=172;
    void updateLag(uint8_t *rdram,int32_t value) {
        const unsigned mode=MEM_BU(0,render_mode);
        MEM_W(0,lag)=std::max(value,(mode==3||mode==4)?1:2);
    }
}
extern "C" void dk64_vita_calculate_lag(uint8_t *rdram) {
    MEM_W(0,lag)=std::max(MEM_W(0,lag),2);
    const unsigned mode=MEM_BU(0,0xffffffff80755318ULL),copy=MEM_BU(0,0xffffffff80755314ULL);
    const bool dktv=(mode==3||mode==4||copy==3||copy==4) && MEM_W(0,map_address)==japes;
    if(dktv) MEM_W(0,lag)=3;
    else if(MEM_BU(0,0xffffffff8076af14ULL)) {
        const int32_t boost=std::max(1,int32_t(uint32_t(MEM_W(0,frame_count))-uint32_t(MEM_W(0,last_frame))));
        unsigned index=MEM_BU(0,history_index);
        MEM_H(index*2,history)=boost;
        index=(index+1)&7; MEM_B(0,history_index)=index;
        const uint32_t old=uint32_t(MEM_W(0,lag));
        const unsigned count=old>=4?1:old<uint32_t(boost)?2:4;
        int32_t maximum=1,minimum=20;
        for(unsigned i=0;i<count;++i) {
            index=(index-1)&7;
            const int32_t sample=MEM_H(index*2,history);
            maximum=std::max(maximum,sample); minimum=std::min(minimum,sample);
        }
        if((old<uint32_t(boost) && old<uint32_t(minimum)) || (uint32_t(boost)<old && uint32_t(maximum)<old)) updateLag(rdram,boost);
        if(uint32_t(MEM_W(0,object_timer))>10)
            while(uint32_t(MEM_W(0,last_frame))+uint32_t(MEM_W(0,lag))>uint32_t(MEM_W(0,frame_count))) dk64_vita_frame_wait(rdram);
        MEM_W(0,last_frame)=MEM_W(0,frame_count);
        return;
    }
    if(!dktv) updateLag(rdram,int32_t(uint32_t(MEM_W(0,frame_count))-uint32_t(MEM_W(0,last_frame))));
    MEM_W(0,last_frame)=MEM_W(0,frame_count);
}

extern "C" int32_t dk64_vita_frame_delta(uint8_t *rdram) {
    const unsigned mode=MEM_BU(0,render_mode);
    if(mode==3||mode==4) return 1;
    const int32_t map=MEM_W(0,map_address);
    const bool story_started=MEM_W(0,0xffffffff807f5ce0ULL)||MEM_W(0,0xffffffff807f5ce4ULL);
    const bool story_map=map==helm_story||map==rock_story||map==dk_house||map==theatre;
    if((!story_started||!story_map) && MEM_BU(0,0xffffffff80746830ULL)) return 0;
    if(!(MEM_HU(0,0xffffffff807f5cf4ULL)&4)) {
        const int32_t frame=MEM_W(0,object_timer);
        const int32_t scene=MEM_H(0,0xffffffff807476f4ULL);
        if(map==rap) {
            const int32_t timer=MEM_HU(0,0xffffffff8075531cULL);
            const int windows[][2]={{96,97},{98,100},{121,127},{133,136}};
            for(const auto &window:windows)
                if(timer<0x1644-window[0]*30 && timer>0x1644-window[1]*30 && frame%8==0) return 3;
        } else if(map==rock_story) {
            if(scene==0 && frame%8==0) return 3;
            if(scene==1 && MEM_HU(0,0xffffffff807476f0ULL)<150 && frame%5<2) return 3;
        } else if(map==helm_story) {
            if(scene==0 && frame%25<8) return 3;
            if(scene==4 && frame%3<2) return 3;
            if(scene==8 && frame%10<7) return 3;
            if(scene>8 && scene<=14 && frame%8==0) return 3;
        } else if(map==theatre) {
            if(scene==7 && frame%6==0) return 3;
        } else if(map==dk_house) {
            if(scene==0 && frame%7==0) return 3;
        }
    }
    return 2;
}
