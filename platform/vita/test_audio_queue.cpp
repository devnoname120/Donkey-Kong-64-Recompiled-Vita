// Exercise the real generated audio-sizing routine and runtime AI query against
// a device that drains complete buffers. This isolates queue feedback from RSP
// synthesis, host scheduling and emulator playback.
#include "audio_queue.h"
#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_80601EE4(uint8_t *,recomp_context *);
static size_t queued=0,device_frames=1024;
static bool reserve_enabled=true;
static constexpr uint32_t rate=22050;
static constexpr gpr next_buffer=0xffffffff80200000ULL,previous_buffer=0xffffffff80201000ULL;
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
extern "C" void osVirtualToPhysical_recomp(uint8_t *,recomp_context *ctx) { ctx->r2=uint32_t(ctx->r4)&0x1fffffff; }
extern "C" void func_global_asm_80602314(uint8_t *,recomp_context *) {}
extern "C" void osAiGetLength_recomp(uint8_t *,recomp_context *ctx) { ctx->r2=ultramodern::get_remaining_audio_bytes(); }
extern "C" void osAiSetNextBuffer_recomp(uint8_t *,recomp_context *ctx) {
    check(ctx->r5%4==0,"AI submission split a stereo frame");queued+=ctx->r5/4;
}
extern "C" void n_alAudioFrame(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r7==552 || ctx->r7==736 || ctx->r7==920,"unexpected generated frame count");
    MEM_W(0,ctx->r5)=0;ctx->r2=0; // No synthesis commands in this sizing fixture.
}
extern "C" void func_global_asm_8060EE58(uint8_t *,recomp_context *) { throw std::runtime_error("unexpected task path"); }
extern "C" void osSendMesg_recomp(uint8_t *,recomp_context *) { throw std::runtime_error("unexpected task message"); }
extern "C" void osWritebackDCacheAll_recomp(uint8_t *,recomp_context *) { throw std::runtime_error("unexpected cache flush"); }

int main() {
    try {
        ultramodern::audio_callbacks_t callbacks{};
        callbacks.get_frames_remaining=[] { return reserve_enabled?vita_audio_frames_remaining(queued,rate,device_frames):queued; };
        ultramodern::set_audio_callbacks(callbacks);ultramodern::set_audio_frequency(rate);
        std::vector<uint8_t> memory(8*1024*1024);auto *rdram=memory.data();
        size_t baseline_padding=0,fixed_padding=0,max_queued=0;unsigned runs=0;
        for(bool enabled:{false,true}) for(size_t block:{256U,512U,1024U}) for(unsigned phase=0;phase<735;phase+=21) {
            reserve_enabled=enabled;device_frames=block;queued=0;
            MEM_W(0,0xffffffff80770558ULL)=0; // Two-task adjustment cooldown.
            MEM_W(0,0xffffffff80770194ULL)=552;MEM_W(0,0xffffffff8077019cULL)=920;
            MEM_W(0,0xffffffff807452c8ULL)=0;
            MEM_W(0,next_buffer)=int32_t(0x80010000);MEM_W(0,previous_buffer)=int32_t(0x80020000);
            MEM_H(4,previous_buffer)=736;
            // Time is in 22,050-Hz sample frames: two VIs are exactly 735.
            size_t game_time=phase,device_time=0,padding=0;
            while(game_time<rate*30 || device_time<rate*30) {
                if(game_time<=device_time) {
                    recomp_context ctx{};ctx.r29=0xffffffff80600000ULL;ctx.r4=next_buffer;ctx.r5=previous_buffer;
                    func_global_asm_80601EE4(rdram,&ctx);
                    check(ctx.r29==0xffffffff80600000ULL,"audio sizing changed caller stack");
                    MEM_H(4,previous_buffer)=MEM_H(4,next_buffer);
                    if(enabled)max_queued=std::max(max_queued,queued);
                    game_time+=735;
                } else {
                    if(queued<block && device_time>rate*2)padding+=block-queued;
                    queued=queued>block?queued-block:0;device_time+=block;
                }
            }
            if(enabled)fixed_padding+=padding;else if(block==1024)baseline_padding+=padding;
            ++runs;
        }
        check(baseline_padding>0,"fixture did not reproduce the original underruns");
        check(fixed_padding==0,"queue reserve left steady-state underruns");
        check(max_queued<=4096,"queue feedback accumulated unbounded latency");
        check(vita_audio_frames_remaining(20,rate,1024)==0,"queue reserve underflowed");
        check(vita_audio_frames_remaining(1000,rate,256)==633,"desktop one-VI reserve was lost");
        std::printf("Audio queue: %u generated-function runs, original padding=%zu frames, fixed padding=%zu, max queued=%zu frames\n",runs,baseline_padding,fixed_padding,max_queued);
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
