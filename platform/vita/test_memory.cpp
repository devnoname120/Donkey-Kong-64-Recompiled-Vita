// Host test of Vita's cached/uncached guest address aliases.
#define __vita__ 1
#include "../../lib/N64ModernRuntime/N64Recomp/include/recomp.h"
#include <vector>
#include <cstdio>
int main() {
    std::vector<uint32_t> words(1024*1024);
    auto *rdram=reinterpret_cast<uint8_t *>(words.data());
    const gpr cached=0xffffffff802fe1c0ULL,uncached=0xffffffffa02fe1c0ULL;
    MEM_W(0,cached)=int32_t(0xad170014);
    if(uint32_t(MEM_W(0,uncached))!=0xad170014) return 1;
    if(MEM_BU(0,uncached)!=0xad || MEM_HU(2,uncached)!=0x14) return 2;
    MEM_B(1,uncached)=0x55;
    if(uint32_t(MEM_W(0,cached))!=0xad550014) return 3;
    SD(0x1122334455667788ULL,8,uncached);
    if(LD(8,cached)!=0x1122334455667788ULL) return 4;
    MEM_W(-4,uncached)=0x12345678;
    if(MEM_W(-4,cached)!=0x12345678) return 5;
    std::puts("Vita KSEG0/KSEG1 aliases: word, byte, halfword, doubleword and negative-offset accesses passed");
}
