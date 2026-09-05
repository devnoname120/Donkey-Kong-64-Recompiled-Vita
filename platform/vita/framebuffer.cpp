#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/renderer_context.hpp"
#include "log.h"
#include <stdexcept>

extern "C" void dk64_vita_sync_framebuffer(uint8_t *rdram,uint32_t address,uint32_t width,uint32_t height) {
    address&=0x1fffffff;
    if(width>1024 || height>1024) throw std::runtime_error("DK64 framebuffer size is invalid");
    const uint32_t size=width*height*2;
    if(address>recomp::mem_size || size>recomp::mem_size-address)
        throw std::runtime_error("DK64 framebuffer is outside RDRAM");
    auto bytes=ultramodern::renderer::read_framebuffer(address,size);
    if(bytes.empty()) { vita_log("Framebuffer CPU sync: no resident image at %08x",address); return; }
    if(bytes.size()!=size) throw std::runtime_error("DK64 framebuffer readback size mismatch");
    for(uint32_t i=0;i<size;++i) rdram[(address+i)^3]=bytes[i];
#if DK64_VITA_DIAGNOSTICS
    uint32_t colored_pixels=0;
    for(uint32_t i=0;i<size;i+=2) colored_pixels+=bytes[i] || (bytes[i+1]&0xfe);
    vita_log("Framebuffer CPU sync: %ux%u RGBA16 at %08x, colored pixels=%u/%u",
        width,height,address,colored_pixels,width*height);
#endif
}
