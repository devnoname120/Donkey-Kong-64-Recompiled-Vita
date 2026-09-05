#include "recomp.h"
#include "librecomp/memory_writes.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <thread>

static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
static std::map<uint32_t,uint8_t> collect() {
    std::map<uint32_t,uint8_t> result;
    for(const auto &write:recomp::collect_memory_writes())
        for(unsigned i=0;i<32;++i) if(write.mask&(1U<<i)) result.emplace(write.address+i,write.bytes[i]);
    return result;
}
int main() {
    try {
        std::vector<uint32_t> memory(0x10000/4,0xaaaaaaaa);
        auto *rdram=reinterpret_cast<uint8_t *>(memory.data());
        recomp::initialize_memory_writes(rdram,memory.size()*4);
        recomp::watch_memory_writes(0x1000,0x1000,true);
        auto verify=[&](unsigned first,unsigned count) {
            auto writes=collect(); check(writes.size()==count,"Wrong number of written bytes");
            for(unsigned i=0;i<count;++i) check(writes.at(first+i)==rdram[(first+i)^3],"Recorded byte order differs from RDRAM");
            check(collect().empty(),"Collected writes were delivered twice");
        };
        // Values equal to existing RAM must still report a store. Use both
        // KSEG aliases and all aligned integer/floating-point store widths.
        do_sb(rdram,0,0xffffffffa0001100ULL,0xaa); verify(0x1100,1);
        do_sh(rdram,0,0xffffffff80001102ULL,0xaaaa); verify(0x1102,2);
        do_sw(rdram,0,0xffffffffa0001104ULL,0xaaaaaaaa); verify(0x1104,4);
        SD(0xaaaaaaaaaaaaaaaaULL,0,0xffffffff80001108ULL); verify(0x1108,8);
        for(unsigned width:{4U,8U}) for(unsigned right:{0U,1U}) for(unsigned offset=0;offset<width;++offset) {
            for(unsigned i=0;i<8;++i) rdram[(0x1120+i)^3]=0xaa;
            const uint64_t value=width==4?0x11223344ULL:0x1122334455667788ULL;
            const unsigned first=right?0:offset,count=right?offset+1:width-offset;
            if(width==4) (right?do_swr:do_swl)(rdram,offset,0xffffffff80001120ULL,value);
            else (right?do_sdr:do_sdl)(rdram,offset,0xffffffffa0001120ULL,value);
            auto writes=collect(); check(writes.size()==count,"Unaligned store reported preserved bytes");
            for(unsigned i=0;i<width;++i) {
                const bool written=i>=first && i<first+count;
                const unsigned source=right?width-count+i:i-offset;
                const uint8_t expected=written?uint8_t(value>>((width-source-1)*8)):0xaa;
                check(rdram[(0x1120+i)^3]==expected,"Unaligned store changed the wrong big-endian bytes");
                check(writes.count(0x1120+i)==unsigned(written),"Unaligned store notification range is wrong");
                if(written) check(writes.at(0x1120+i)==expected,"Unaligned store payload is wrong");
            }
        }
        do_sw(rdram,0,0xffffffff80005000ULL,0); check(collect().empty(),"Unwatched page produced writes");
        // A bulk native write can start/end on unwatched pages while crossing
        // a watched page in the middle.
        recomp_notify_memory_write(rdram,0x800,0x2800); verify(0x1000,0x1000);
        recomp::watch_memory_writes(0x1100,32,true);
        recomp::watch_memory_writes(0x1000,0x1000,false);
        do_sb(rdram,0,0x1100,7); verify(0x1100,1);
        do_sb(rdram,0,0x1100,8);
        recomp::watch_memory_writes(0x1100,32,false);
        recomp::watch_memory_writes(0x1000,0x2000,true);
        check(collect().empty(),"Rewatching a page replayed retired writes");

        std::atomic<unsigned> finished{0};
        auto producer=[&](unsigned base) {
            for(unsigned i=0;i<4096;i+=4) do_sw(rdram,i,base,0x12345678);
            ++finished;
        };
        std::thread a(producer,0x1000),b(producer,0x2000);
        std::map<uint32_t,uint8_t> seen;
        while(finished.load()!=2) {
            auto writes=collect(); seen.insert(writes.begin(),writes.end()); std::this_thread::yield();
        }
        a.join(); b.join();
        auto final=collect(); seen.insert(final.begin(),final.end());
        check(seen.size()==8192,"Concurrent producer/consumer lost write notifications");
        for(unsigned i=0;i<8192;++i) check(seen.at(0x1000+i)==uint8_t(0x12345678U>>((3-(i&3))*8)),"Concurrent write payload is wrong");
        std::puts("Memory-write checks: same-value stores, KSEG aliases, all unaligned offsets, bulk writes, watch lifetime and concurrent collection passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
