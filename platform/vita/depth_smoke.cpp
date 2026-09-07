#include "fast/tests/rt64_fast_depth_checks.h"
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <cstdio>

int _newlib_heap_size_user=128*1024*1024;
unsigned int sceUserMainThreadStackSize=2*1024*1024;

int main() {
    sceIoMkdir("ux0:data/rt64-depth",0777);
    FILE *results=std::fopen("ux0:data/rt64-depth/results.log","w");
    if(!results)return 1;
    if(!std::freopen("ux0:data/rt64-depth/error.log","w",stderr))return 1;
    std::setvbuf(results,nullptr,_IONBF,0);
    std::setvbuf(stderr,nullptr,_IONBF,0);
    try {
        auto sink=RT64::createFastVitaGLSink(false,false);
        unsigned cases=0;uint32_t address=0x100000;
        for(bool batching:{false,true}) {
            if(batching)sink=RT64::createFastBatchingSink(std::move(sink));
            for(const auto dimensions:std::array<std::array<unsigned,2>,3>{{{32,24},{37,19},{320,240}}}) {
                std::fprintf(results,"Depth case: width=%u height=%u batching=%u\n",dimensions[0],dimensions[1],unsigned(batching));
                cases+=RT64::Tests::Depth::run(*sink,dimensions[0],dimensions[1],address);
                address+=0x100000;
                std::fprintf(results,"Depth case passed: total=%u\n",cases);
            }
        }
        for(unsigned batch=0;batch<4;++batch) {
            const uint64_t started=sceKernelGetProcessTimeWide();
            RT64::Tests::Depth::stress(*sink,25,address);
            std::fprintf(results,"Depth stress passed: iterations=%u batch_us=%llu\n",(batch+1)*25,
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-started));
        }
        for(unsigned batch=0;batch<4;++batch) {
            const uint64_t started=sceKernelGetProcessTimeWide();
            RT64::Tests::Depth::stress(*sink,25,address,true);
            std::fprintf(results,"Depth presentation stress passed: iterations=%u batch_us=%llu\n",(batch+1)*25,
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-started));
        }
        for(unsigned batch=0;batch<5;++batch) {
            RT64::Tests::Depth::clear(*sink,address,320,240,{0,0,320,240});
            RT64::Tests::Depth::rectangle(*sink,address+0x40000,address,320,240,{0,0,320,240},0.5f);
            std::vector<uint8_t> pixel;
            sink->readDepthFramebuffer(address,2,pixel);
            const uint64_t started=sceKernelGetProcessTimeWide();
            for(unsigned i=0;i<1000;++i) {
                RT64::Tests::Depth::rectangle(*sink,address+0x40000,address,320,240,{0,0,320,240},0.5f);
                sink->flushDraws();
            }
            const uint64_t submitted=sceKernelGetProcessTimeWide();
            RT64::Tests::Depth::check(sink->readDepthFramebuffer(address,2,pixel)
                && pixel==std::vector<uint8_t>({0x20,0}),"submission workload depth mismatch");
            std::fprintf(results,"Submission stress: draws=1000 submit_us=%llu completed_us=%llu\n",
                static_cast<unsigned long long>(submitted-started),
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-started));
            sink->present(address+0x40000);
        }
        sink.reset();
        std::fprintf(results,"PASS: %u exact depth images and partial-byte queries\n",cases);
        std::fclose(results);
        return 0;
    } catch(const std::exception &error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        std::fclose(results);
        return 1;
    }
}
