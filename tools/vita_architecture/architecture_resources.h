#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <vector>
namespace ArchitectureProbe {
struct Resources {
    std::map<std::pair<uint32_t,uint32_t>,std::vector<uint8_t>> data;
    uint64_t calls=0,bytes=0,hits=0,hitBytes=0,changes=0,stored=0;
    bool capped=false;
    void observe(uint32_t address,const uint8_t *source,uint32_t size) {
        ++calls;bytes+=size;if(!size)return;
        auto found=data.find({address,size});
        if(found!=data.end()) {
            if(!std::memcmp(found->second.data(),source,size)) { ++hits;hitBytes+=size;return; }
            ++changes;found->second.assign(source,source+size);return;
        }
        if(stored+size>8*1024*1024 || data.size()>=16384) { capped=true;return; }
        data.emplace(std::make_pair(address,size),std::vector<uint8_t>(source,source+size));stored+=size;
    }
    void write(FILE *f) const {
        std::fprintf(f,"{\"calls\":%llu,\"source_bytes\":%llu,\"unchanged_address_hits\":%llu,\"unchanged_address_bytes\":%llu,\"changed_address_hits\":%llu,\"unique_ranges\":%u,\"retained_bytes\":%llu,\"capped\":%s}",
            (unsigned long long)calls,(unsigned long long)bytes,(unsigned long long)hits,(unsigned long long)hitBytes,
            (unsigned long long)changes,unsigned(data.size()),(unsigned long long)stored,capped?"true":"false");
    }
};
}
