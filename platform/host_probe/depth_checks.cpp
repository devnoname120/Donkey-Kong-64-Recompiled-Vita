#include "egl.h"
#include "fast/tests/rt64_fast_depth_checks.h"

namespace { bool batching=false; }
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }
unsigned probePauseGeneration() { return 0; }

int main() {
    try {
        auto platform=createProbeEGL(".");unsigned cases=0;
        for(bool merge:{false,true})for(const auto dimensions:std::array<std::array<unsigned,2>,3>{{{32,24},{37,19},{320,240}}}) {
            batching=merge;auto sink=platform->createSink();
            cases+=RT64::Tests::Depth::run(*sink,dimensions[0],dimensions[1],0x100000);
        }
        auto sink=platform->createSink();
        RT64::Tests::Depth::stress(*sink,100,0x100000);
        std::puts("Depth stress: 100 full-size redraws and first pixel reads passed");
        RT64::Tests::Depth::stress(*sink,100,0x100000,true);
        std::puts("Depth presentation stress: 100 full-size redraws, pixel reads and presents passed");
        std::printf("Depth readback: %u exact images, first reads, redraws, shared/independent Z, partial clears, odd byte ranges and batching passed\n",cases);
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
