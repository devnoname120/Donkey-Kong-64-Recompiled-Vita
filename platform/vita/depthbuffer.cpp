#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/renderer_context.hpp"
#include "log.h"
#include <stdexcept>

extern "C" void dk64_vita_read_depth(uint8_t *rdram,recomp_context *ctx) {
    const int32_t x=int16_t(ctx->r4),y=int16_t(ctx->r5);
    const gpr view=MEM_W(0,0xffffffff807fc924ULL);
    const uint32_t view_offset=uint32_t(view)&0x1fffffff;
    if(view_offset>recomp::mem_size || recomp::mem_size-view_offset<0x278)
        throw std::runtime_error("DK64 depth query viewport is outside RDRAM");
    if(x<MEM_H(0x270,view) || x>MEM_H(0x274,view)
        || y<MEM_H(0x272,view) || y>MEM_H(0x276,view))return;
    const uint32_t base=uint32_t(MEM_W(0,0xffffffff8076a060ULL))&0x1fffffff;
    const int32_t width=MEM_H(0,0xffffffff80744490ULL);
    const int64_t address=int64_t(base)+(int64_t(width)*y+x)*2;
    if(address<0 || address+2>recomp::mem_size)
        throw std::runtime_error("DK64 depth query is outside RDRAM");
#if DK64_VITA_DIAGNOSTICS
    static unsigned queries=0;
    const bool trace=queries++<16;
    if(trace)vita_log("Depth query request: address=%08x pixel=%d,%d width=%d",uint32_t(address),x,y,width);
#endif
    const auto bytes=ultramodern::renderer::read_depthbuffer(uint32_t(address),2);
#if DK64_VITA_DIAGNOSTICS
    if(trace)vita_log("Depth query response: bytes=%u value=%04x",unsigned(bytes.size()),bytes.size()==2?(unsigned(bytes[0])<<8)|bytes[1]:0);
#endif
    if(bytes.empty())return;
    if(bytes.size()!=2)throw std::runtime_error("DK64 depth readback size mismatch");
    rdram[uint32_t(address)^3]=bytes[0];
    rdram[(uint32_t(address)+1)^3]=bytes[1];
}
