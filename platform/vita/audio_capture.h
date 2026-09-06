#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// Probe-only storage. The caller serializes access with its existing audio
// mutex. Nothing is written to disk on the audio producer thread.
struct AudioCaptureData {
    struct Chunk { uint64_t time_us;uint32_t first_frame,frames; };
    struct QueueQuery { uint64_t time_us;uint32_t frames; };
    uint32_t rate=0;
    const char *reason="sample limit";
    std::vector<int16_t> samples;
    std::vector<Chunk> chunks;
    std::vector<QueueQuery> queries;
};

class AudioCapture {
    AudioCaptureData data;
    size_t written=0;
    bool finished=false,taken=false;
public:
    void set_rate(uint32_t rate) {
        if(taken || finished || rate==data.rate) return;
        if(written) { data.reason="sample-rate change";finished=true;return; }
        if(!rate || rate>96000) { data={};return; }
        data.rate=rate;
        data.samples=std::vector<int16_t>(size_t(rate)*2*60);
        data.chunks.clear();data.chunks.reserve(8192);
        data.queries.clear();data.queries.reserve(8192);
    }
    void observe_queue(uint32_t frames,uint64_t time_us) {
        if(!finished && !taken && data.rate && data.queries.size()<8192)
            data.queries.push_back({time_us,frames});
    }
    void append(const int16_t *samples,size_t count,uint64_t time_us) {
        if(finished || taken || !data.rate || !count) return;
        if(data.chunks.size()==8192) { data.reason="chunk limit";finished=true;return; }
        const size_t size=std::min(count,data.samples.size()-written)&~size_t(1);
        if(size) {
            data.chunks.push_back({time_us,uint32_t(written/2),uint32_t(size/2)});
            std::copy_n(samples,size,data.samples.data()+written);written+=size;
        }
        if(written==data.samples.size()) finished=true;
    }
    bool ready(uint64_t time_us) const {
        return !taken && written && (finished || (time_us>=data.chunks.front().time_us
            && time_us-data.chunks.front().time_us>=70000000));
    }
    AudioCaptureData take() {
        if(taken) return {};
        if(!finished) data.reason="time limit";
        taken=true;finished=true;data.samples.resize(written);
        return std::move(data);
    }
};
