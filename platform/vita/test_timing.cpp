#include "recomp.h"
#include <cstdio>
#include <vector>
#include <stdexcept>
extern "C" void dk64_vita_calculate_lag(uint8_t *);
extern "C" int32_t dk64_vita_frame_delta(uint8_t *);
extern "C" double dk64_vita_scale_rabbit_speed(uint8_t *,double);
extern "C" int32_t dk64_vita_klamour_round_duration(int32_t);
extern "C" int32_t dk64_vita_klamour_tick(uint8_t *,int32_t);
static unsigned waits=0;
extern "C" void dk64_vita_frame_wait(uint8_t *rdram) { ++waits; ++MEM_W(0,0xffffffff80767cc4ULL); }
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        std::vector<uint8_t> memory(8*1024*1024); auto *rdram=memory.data();
        check(dk64_vita_frame_delta(rdram)==2,"normal frame interval");
        MEM_B(0,0xffffffff807444ecULL)=3;
        check(dk64_vita_frame_delta(rdram)==1,"arcade frame interval");
        MEM_B(0,0xffffffff807444ecULL)=0; MEM_B(0,0xffffffff80746830ULL)=1;
        check(dk64_vita_frame_delta(rdram)==0,"loading frame interval");
        MEM_W(0,0xffffffff807f5ce4ULL)=1; MEM_W(0,0xffffffff8076a0a8ULL)=153;
        check(dk64_vita_frame_delta(rdram)==2,"story loading guard");
        MEM_B(0,0xffffffff80746830ULL)=0; MEM_W(0,0xffffffff8076a0a8ULL)=76;
        MEM_H(0,0xffffffff8075531cULL)=2800; MEM_W(0,0xffffffff8076a064ULL)=16;
        check(dk64_vita_frame_delta(rdram)==3,"rap timing correction");
        MEM_W(0,0xffffffff8076a064ULL)=17;
        check(dk64_vita_frame_delta(rdram)==2,"rap normal interval");
        MEM_W(0,0xffffffff8076a0a8ULL)=172; MEM_H(0,0xffffffff807476f4ULL)=1;
        MEM_W(0,0xffffffff8076a064ULL)=10; MEM_H(0,0xffffffff807476f0ULL)=149;
        check(dk64_vita_frame_delta(rdram)==3,"rock story correction");
        MEM_H(0,0xffffffff807476f0ULL)=0xffff;
        check(dk64_vita_frame_delta(rdram)==2,"unsigned story timer");
        MEM_W(0,0xffffffff8076a0a8ULL)=152; MEM_H(0,0xffffffff807476f4ULL)=8;
        MEM_W(0,0xffffffff8076a064ULL)=6;
        check(dk64_vita_frame_delta(rdram)==3,"helm story correction");
        MEM_H(0,0xffffffff807f5cf4ULL)=4;
        check(dk64_vita_frame_delta(rdram)==2,"bonus cutscene disables story correction");
        MEM_H(0,0xffffffff807f5cf4ULL)=0;
        MEM_W(0,0xffffffff80767cc4ULL)=100; MEM_W(0,0xffffffff8076af10ULL)=100;
        dk64_vita_calculate_lag(rdram);
        check(MEM_W(0,0xffffffff80744478ULL)==2,"zero lag was not clamped");
        MEM_W(0,0xffffffff80767cc4ULL)=101; MEM_B(0,0xffffffff807444ecULL)=4;
        dk64_vita_calculate_lag(rdram);
        check(MEM_W(0,0xffffffff80744478ULL)==1,"minigame lag limit");
        MEM_B(0,0xffffffff807444ecULL)=0; MEM_B(0,0xffffffff8076af14ULL)=1;
        for(unsigned i=0;i<8;++i) MEM_H(i*2,0xffffffff8076af00ULL)=2;
        MEM_W(0,0xffffffff80744478ULL)=2; MEM_W(0,0xffffffff8076a064ULL)=11;
        dk64_vita_calculate_lag(rdram);
        check(waits==2 && MEM_W(0,0xffffffff8076af10ULL)==103,"cooperative frame wait");
        MEM_B(0,0xffffffff80755318ULL)=3; MEM_W(0,0xffffffff8076a0a8ULL)=7;
        dk64_vita_calculate_lag(rdram);
        check(MEM_W(0,0xffffffff80744478ULL)==3 && waits==2,"DKTV fixed lag");
        MEM_W(0,0xffffffff80744478ULL)=2;
        check(int(dk64_vita_scale_rabbit_speed(rdram,70.0))==66,"rabbit speed at two-frame lag");
        MEM_W(0,0xffffffff80744478ULL)=3;
        check(int(dk64_vita_scale_rabbit_speed(rdram,70.0))==100,"rabbit speed at three-frame lag");
        MEM_W(0,0xffffffff80744478ULL)=6;
        check(int(dk64_vita_scale_rabbit_speed(rdram,70.0))==200,"rabbit speed at six-frame lag");
        MEM_W(0,0xffffffff80744478ULL)=2;
        check(int(dk64_vita_scale_rabbit_speed(rdram,22.5))==21,"rabbit bonus speed must be scaled before integer truncation");
        check(dk64_vita_klamour_round_duration(48)==129,"easy/normal Klamour duration overflowed a signed byte");
        check(dk64_vita_klamour_round_duration(30)==81,"hard/insane Klamour duration");
        check(dk64_vita_klamour_round_duration(50)==50,"default Klamour duration must remain unscaled");
        check(dk64_vita_klamour_tick(rdram,162)==160,"Klamour initial timer decrement");
        check(dk64_vita_klamour_tick(rdram,-79)==-81,"Klamour reset boundary must not trigger early");
        check(dk64_vita_klamour_tick(rdram,-80)==-82,"Klamour timer must cross the negative reset boundary");
        MEM_W(0,0xffffffff80744478ULL)=3;
        check(dk64_vita_klamour_tick(rdram,1)==-2,"Klamour signed timer crossing");
        std::puts("DK64 timing: frame intervals, waits, DKTV, rabbit speed and Klamour durations passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
