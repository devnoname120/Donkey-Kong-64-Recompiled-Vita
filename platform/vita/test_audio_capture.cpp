#include "audio_capture.h"
#include <array>
#include <cstdio>
#include <stdexcept>

static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        const std::array<int16_t,8> first={1,-1,2,-2,3,-3,4,-4};
        AudioCapture capture;capture.set_rate(48000);capture.set_rate(22050);
        capture.append(first.data(),first.size(),1000);
        capture.observe_queue(1234,1001);
        check(!capture.ready(999),"old main-thread timestamp ended capture early");
        check(!capture.ready(70000999)&&capture.ready(70001000),"capture wall-time bound");
        auto result=capture.take();check(result.rate==22050&&result.samples==std::vector<int16_t>(first.begin(),first.end()),"capture changed PCM");
        check(result.chunks.size()==1&&result.chunks[0].time_us==1000&&result.chunks[0].frames==4,"capture chunk metadata");
        check(result.queries.size()==1&&result.queries[0].time_us==1001&&result.queries[0].frames==1234,"queue observation changed units or timestamp");
        capture.append(first.data(),first.size(),80000000);capture.set_rate(32000);
        check(!capture.ready(80000000)&&capture.take().samples.empty(),"capture restarted after export");
        AudioCapture short_capture;short_capture.set_rate(1);
        std::vector<int16_t> samples(124,7);short_capture.append(samples.data(),samples.size(),100);
        check(short_capture.ready(100)&&short_capture.take().samples.size()==120,"capture did not clamp to complete stereo frames");
        AudioCapture changed;changed.set_rate(22050);changed.append(first.data(),4,10);changed.append(first.data()+4,4,20);changed.set_rate(32000);
        check(changed.ready(20),"sample-rate change did not finish current capture");
        result=changed.take();check(result.rate==22050&&result.chunks.size()==2&&result.chunks[1].first_frame==2&&result.samples.size()==8,"sample-rate change mixed formats");
        AudioCapture invalid;invalid.set_rate(22050);invalid.set_rate(0);invalid.append(first.data(),first.size(),10);
        check(!invalid.ready(80000000),"invalid rate retained a stale PCM format");
        std::puts("Audio capture: exact PCM, chunk positions, bounded duration, clock ordering, rate changes and one-time export passed");
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
