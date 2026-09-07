#include "trace_sink.h"
#include "hle/rt64_vi.h"
#include <stdexcept>

namespace {
void check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
class Backend final : public RT64::FastDrawSink {
public:
    unsigned draws=0,flushes=0,syncs=0,presents=0,depth_reads=0,color_reads=0;
    bool throw_depth=false;
    const uint8_t *memory=nullptr;
    size_t memory_size=0;
    void draw(const RT64::FastDraw &) override { ++draws; }
    void fullSync() override { ++syncs; }
    void flushDraws() override { ++flushes; }
    void present(uint32_t address) override { check(address==0x200000,"trace changed scanout address");++presents; }
    void present(const RT64::VI &vi) override { check(vi.origin==0x200280,"trace discarded VI state");++presents; }
    void setRDRAM(const uint8_t *data,size_t size) override { memory=data;memory_size=size; }
    bool readFramebuffer(uint32_t address,uint32_t size,std::vector<uint8_t> &bytes) override {
        ++color_reads;check(address==0x200000 && size==2,"trace changed color query");bytes={0xf8,1};return true;
    }
    bool readDepthFramebuffer(uint32_t address,uint32_t size,std::vector<uint8_t> &bytes) override {
        ++depth_reads;
        if(throw_depth)throw std::runtime_error("backend depth failure");
        if(address!=0x100003)return false;
        check(size==3,"trace changed partial depth query");bytes={0x34,0x56,0x78};return true;
    }
};
}
void vita_log(const char *,...) {}
int main() {
    try {
        auto backend=std::make_unique<Backend>();auto *observed=backend.get();
        TraceSink trace(std::move(backend),"unused-trace-fixture");
        std::vector<uint8_t> bytes{0xaa};
        check(trace.readDepthFramebuffer(0x100003,3,bytes),"trace sink dropped a resident depth query");
        check(bytes==std::vector<uint8_t>({0x34,0x56,0x78}) && observed->depth_reads==1,"trace altered depth bytes");
        bytes={0xaa};check(!trace.readDepthFramebuffer(0x300000,3,bytes) && bytes==std::vector<uint8_t>({0xaa}),"trace altered a missing depth image");
        observed->throw_depth=true;bool threw=false;
        try { trace.readDepthFramebuffer(0x100003,3,bytes); }
        catch(const std::runtime_error &error) { threw=std::string(error.what())=="backend depth failure"; }
        check(threw,"trace swallowed a depth backend error");
        trace.draw({});trace.flushDraws();trace.fullSync();trace.present(0x200000);
        RT64::VI vi{};vi.origin=0x200280;trace.present(vi);
        trace.setRDRAM(bytes.data(),bytes.size());
        check(observed->draws==1 && observed->flushes==1 && observed->syncs==1 && observed->presents==2,"trace dropped rendering operations");
        check(observed->memory==bytes.data() && observed->memory_size==bytes.size(),"trace changed guest memory");
        check(trace.readFramebuffer(0x200000,2,bytes) && bytes==std::vector<uint8_t>({0xf8,1}) && observed->color_reads==1,"trace changed color readback");
        std::puts("Trace sink: depth bytes, misses, exceptions, color reads, draw/flush/sync and VI forwarding passed");
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
