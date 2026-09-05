// Host differential test of the ARMv7 fallback against native 128-bit math.
// Compile with clang++ -std=c++17 -fsanitize=undefined,address this-file.cpp.
#undef __SIZEOF_INT128__
#include "../../lib/N64ModernRuntime/N64Recomp/include/recomp.h"
#include <cstdio>

int main() {
    uint64_t seed = 0x5eeda123456789abULL;
    auto random = [&]() { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; };
    const uint64_t edges[] = {0,1,2,0x7fffffff,0x80000000,0xffffffff,
        0x100000000ULL,0x7fffffffffffffffULL,0x8000000000000000ULL,UINT64_MAX};
    auto verify = [](uint64_t a, uint64_t b) {
        uint64_t low,high; DMULTU(a,b,&low,&high);
        const unsigned __int128 u = (unsigned __int128)a*b;
        if (low != uint64_t(u) || high != uint64_t(u>>64)) return false;
        int64_t slow,shigh; DMULT(int64_t(a),int64_t(b),&slow,&shigh);
        const __int128 s = (__int128)int64_t(a)*int64_t(b);
        return uint64_t(slow)==uint64_t(s) && uint64_t(shigh)==uint64_t(s>>64);
    };
    for(auto a:edges) for(auto b:edges) if(!verify(a,b)) return 1;
    for(unsigned i=0;i<100000;++i) if(!verify(random(),random())) return 1;
    std::puts("ARMv7 DMULT/DMULTU: 100 edge pairs and 100000 random pairs matched native 128-bit results");
}
