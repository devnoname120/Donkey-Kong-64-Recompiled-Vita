#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/renderer_context.hpp"
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void dk64_vita_sync_framebuffer(uint8_t *,uint32_t,uint32_t,uint32_t);
static std::vector<uint8_t> response;
static unsigned calls=0;
static uint32_t requested_address=0,requested_size=0;
void vita_log(const char *,...) {}
std::vector<uint8_t> ultramodern::renderer::read_framebuffer(uint32_t address,uint32_t size) {
    ++calls; requested_address=address; requested_size=size; return response;
}
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        std::vector<uint8_t> memory(recomp::mem_size,0xa5);
        auto *rdram=memory.data();
        response={0xf8,0x01,0x07,0xc1,0x00,0x3f,0xff,0xff};
        dk64_vita_sync_framebuffer(rdram,0xa0000080,2,2);
        check(calls==1 && requested_address==0x80 && requested_size==8,"readback request is incorrect");
        check(uint32_t(MEM_W(0,0xffffffff80000080ULL))==0xf80107c1,"word-swapped first row is incorrect");
        check(uint32_t(MEM_W(0,0xffffffff80000084ULL))==0x003fffff,"word-swapped second row is incorrect");
        check(memory[0x7f]==0xa5 && memory[0x88]==0xa5,"readback modified adjacent memory");
        const auto before=memory;
        response.clear();
        dk64_vita_sync_framebuffer(rdram,0x80000080,2,2);
        check(memory==before,"missing framebuffer destroyed RDRAM");
        bool rejected=false;
        try { dk64_vita_sync_framebuffer(rdram,0x80000000+recomp::mem_size-4,2,2); }
        catch(const std::runtime_error &) { rejected=true; }
        check(rejected && calls==2,"out-of-bounds readback was submitted");
        response={1}; rejected=false;
        try { dk64_vita_sync_framebuffer(rdram,0x80000080,2,2); }
        catch(const std::runtime_error &) { rejected=true; }
        check(rejected && memory==before,"short readback modified RDRAM");
        std::puts("DK64 framebuffer: KSEG aliases, N64 byte order, bounds and unavailable/short reads passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
