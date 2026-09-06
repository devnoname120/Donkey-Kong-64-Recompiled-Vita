#include "egl.h"
#include "gl_audit.h"
#include "hle/rt64_vi.h"
#include "fast/rt64_fast_state.h"
#include "../vita/memory_writes.h"
#include "recomp.h"
#include <GLES2/gl2.h>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

static bool batching=false;
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }
unsigned probePauseGeneration() { return 0; }

static void quad(RT64::FastDraw &draw,float left,float bottom,float right,float top,std::array<float,4> color) {
    const float xy[6][2]={{left,bottom},{right,bottom},{right,top},{left,bottom},{right,top},{left,top}};
    draw.vertices.resize(6);
    for(unsigned i=0;i<6;++i) {
        draw.vertices[i].position[0]=xy[i][0]; draw.vertices[i].position[1]=xy[i][1];
        std::copy(color.begin(),color.end(),draw.vertices[i].color);
    }
}
static std::vector<uint8_t> render(ProbeEGL &platform,bool merge) {
    batching=merge;
    auto sink=platform.createSink();
    RT64::FastDraw draw;
    draw.colorAddress=0x100000; draw.depthAddress=0x200000;
    draw.fill=true; draw.fillColor={0.125f,0.25f,0.375f,1};
    quad(draw,-1,-1,1,1,{1,1,1,1}); sink->draw(draw);
    draw.fill=false;
    draw.combine={0x00ffffff,(15U<<24)|(7U<<21)|(7U<<18)|(4U<<6)|(7U<<3)|4U};
    draw.otherMode.H=G_CYC_1CYCLE;
    draw.otherMode.L=FORCE_BL|(1U<<22); // Source alpha over framebuffer color.
    quad(draw,-0.8f,-0.8f,0.8f,0.8f,{1,0,0,0.5f}); sink->draw(draw);
    quad(draw,-0.4f,-0.4f,0.9f,0.9f,{0,0,1,0.5f}); sink->draw(draw);
    auto red=std::make_shared<RT64::FastTexture>(),green=std::make_shared<RT64::FastTexture>();
    red->width=red->height=green->width=green->height=1;
    red->hash=1; red->rgba={255,0,0,255}; green->hash=2; green->rgba={0,255,0,255};
    draw.textures={red,green}; draw.otherMode={0,G_CYC_2CYCLE};
    // Lerp TEXEL0/TEXEL1 by primitive LOD fraction, then modulate by shade.
    draw.combine={(0xfc26a004U&~((31U<<15)|(7U<<9)))|(14U<<15)|(6U<<9),0x1f1093ff};
    draw.lodFraction=0.25f;
    quad(draw,-0.8f,0.4f,-0.1f,0.8f,{1,1,1,1}); sink->draw(draw);
    draw.lodFraction=0.75f;
    quad(draw,0.1f,0.4f,0.8f,0.8f,{1,1,1,1}); sink->draw(draw);
    sink->fullSync(); sink->present(0x100000);
    std::vector<uint8_t> rgba(960*544*4);
    glReadPixels(0,0,960,544,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("GL check readback failed");
    return rgba;
}
static void checkPresentation(ProbeEGL &platform) {
    batching=true;
    auto sink=platform.createSink();
    RT64::FastDraw draw;
    draw.colorAddress=0x300000; draw.fill=true; draw.fillColor={0.25f,0.5f,0.75f,1};
    quad(draw,-1,-1,1,1,{1,1,1,1}); sink->draw(draw);
    RT64::VI vi{};
    vi.status.word=2; vi.width=320; vi.origin=draw.colorAddress+640;
    vi.hRegion.word=0x006c02ec;
    auto checkPixel=[&](std::array<int,3> expected) {
        uint8_t rgba[4]{};
        glReadPixels(480,272,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("VI readback failed");
        for(unsigned i=0;i<3;++i)
            if(std::abs(int(rgba[i])-expected[i])>2) throw std::runtime_error("VI presentation color mismatch");
    };
    sink->present(vi); checkPixel({64,128,191});
    vi.status.word=0; sink->present(vi); checkPixel({0,0,0});
    vi.status.word=2; vi.hRegion.word=0; sink->present(vi); checkPixel({0,0,0});
    vi.hRegion.word=0x006c02ec; vi.status.word=2|8;
    sink->present(vi); checkPixel({136,186,224});
    vi.status.word=2|64; vi.vCurrentLine=1; vi.origin=draw.colorAddress+1280;
    sink->present(vi); checkPixel({64,128,191});
    vi.origin=0x10000; sink->present(vi); checkPixel({0,0,0});
    sink->present(draw.colorAddress); checkPixel({64,128,191});
}
static void checkUniformStateChanges(ProbeEGL &platform) {
    batching=false;
    auto sink=platform.createSink();
    RT64::FastDraw draw;
    draw.colorAddress=0x400; draw.width=draw.height=8; draw.colorBytes=4;
    draw.otherMode.H=G_CYC_1CYCLE;
    const auto mux=[](unsigned input) {
        return interop::ColorCombiner{0x00ffffff,(15U<<24)|(7U<<21)|(7U<<18)|(input<<6)|(7U<<3)|input};
    };
    draw.combine=mux(3); // Primitive color and alpha.
    draw.primitive={1,0,0,1}; quad(draw,-1,-1,1,1,{1,1,1,1}); sink->draw(draw);
    resetProbeGLStats();
    for(unsigned i=0;i<32;++i) sink->draw(draw);
    const auto repeated=probeGLStats();
    auto checkColor=[&](std::array<uint8_t,4> expected) {
        std::vector<uint8_t> actual;
        if(!sink->readFramebuffer(draw.colorAddress,8*8*4,actual)) throw std::runtime_error("Uniform test framebuffer is missing");
        for(unsigned p=0;p<64;++p) for(unsigned c=0;c<4;++c)
            if(actual[p*4+c]!=expected[c]) throw std::runtime_error("Cached uniform changed rendered color");
    };
    checkColor({255,0,0,255});
    draw.primitive={0,0,1,1}; sink->draw(draw); checkColor({0,0,255,255});
    draw.combine=mux(5); draw.environment={0,1,0,1}; sink->draw(draw); checkColor({0,255,0,255});
    draw.environment={0,0,0,0}; sink->draw(draw); checkColor({0,0,0,0});
    // A different program using the same uniform name must own separate data.
    draw.combine=mux(3); draw.otherMode.L=G_AC_THRESHOLD;
    draw.primitive={0,1,0,1}; sink->draw(draw); checkColor({0,255,0,255});
    draw.otherMode.L=0; draw.primitive={0,0,1,1};
    // Program switches and internal snapshot/presentation programs must not
    // invalidate another program's stored uniform values.
    draw.combine=mux(3); sink->draw(draw); checkColor({0,0,255,255});
    auto snapshot=sink->snapshotFramebuffer(draw.colorAddress,8*8*4);
    if(!snapshot) throw std::runtime_error("Uniform test snapshot is missing");
    sink->present(draw.colorAddress);
    sink->draw(draw); checkColor({0,0,255,255});
    draw.primitive={1,0,0,1}; sink->draw(draw); checkColor({255,0,0,255});
    std::printf("Repeated uniform workload: draws=%llu uniform_calls=%llu\n",
        static_cast<unsigned long long>(repeated.draws),static_cast<unsigned long long>(repeated.uniformCalls));
    if(repeated.draws!=32 || repeated.uniformCalls)
        throw std::runtime_error("Identical draw state resubmitted uniforms to GLES");
}
static void checkTextureUniformStateChanges(ProbeEGL &platform) {
    batching=false;
    auto sink=platform.createSink();
    auto texture=std::make_shared<RT64::FastTexture>();
    texture->width=4; texture->height=1; texture->hash=0x41;
    texture->rgba={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};
    RT64::FastDraw draw; draw.colorAddress=0x1000; draw.width=draw.height=4; draw.colorBytes=4;
    draw.otherMode={0,G_CYC_COPY}; draw.textures[0]=texture;
    draw.tiles[0].cms=draw.tiles[0].cmt=G_TX_CLAMP; draw.tiles[0].lrs=12;
    quad(draw,-1,-1,1,1,{1,1,1,1});
    auto sample=[&](float u,std::array<uint8_t,4> expected) {
        for(auto &v:draw.vertices) { v.uv[0]=u; v.uv[1]=0; }
        sink->draw(draw);
        std::vector<uint8_t> actual;
        if(!sink->readFramebuffer(draw.colorAddress,64,actual)) throw std::runtime_error("Texture uniform framebuffer is missing");
        for(unsigned p=0;p<16;++p) for(unsigned c=0;c<4;++c)
            if(actual[p*4+c]!=expected[c]) throw std::runtime_error("Cached texture uniform changed texel selection");
    };
    sample(0,{255,0,0,255}); sample(1,{0,255,0,255});
    draw.tiles[0].uls=4; sample(1,{255,0,0,255});
    draw.tiles[0].uls=0; draw.tiles[0].shifts=1; sample(3,{0,255,0,255});
    draw.tiles[0].shifts=0; sample(3,{255,255,0,255});
    draw.tiles[0].cms=0; draw.tiles[0].masks=1; sample(3,{0,255,0,255});
    draw.tiles[0].cms=G_TX_MIRROR; sample(3,{255,0,0,255});
    draw.tiles[0].cms=G_TX_CLAMP; draw.tiles[0].lrs=4; sample(3,{0,255,0,255});
    draw.tiles[0].lrs=0; sample(3,{255,0,0,255});
    auto smaller=std::make_shared<RT64::FastTexture>();
    smaller->width=2; smaller->height=1; smaller->hash=0x42;
    smaller->rgba={0,0,255,255, 255,255,0,255};
    draw.textures[0]=smaller; draw.tiles[0].masks=0; draw.tiles[0].lrs=4;
    sample(1,{255,255,0,255});
}
static void checkZipperTextureAlpha(ProbeEGL &platform) {
    for(bool merge:{false,true}) {
        batching=merge;
        auto sink=platform.createSink();
        RT64::FastDraw draw; draw.colorAddress=0x6000; draw.width=draw.height=8; draw.colorBytes=4;
        for(unsigned alpha:{0u,31u,32u,255u}) {
            draw.fill=true; draw.fillColor={0,0,1,1};
            quad(draw,-1,-1,1,1,{0,1,0,0}); sink->draw(draw);
            auto texture=std::make_shared<RT64::FastTexture>();
            texture->width=texture->height=1; texture->hash=0x100+alpha;
            texture->rgba={255,0,0,uint8_t(alpha)};
            draw.fill=false; draw.textures[0]=texture;
            // Original DK64 zipper commands at 0x8070BAF4..0x8070BB1C:
            // G_CC_DECALRGBA and alpha-to-coverage textured edges. Stale
            // primitive/shade colors must not tint the snapshot or fill its seam.
            draw.combine={0x00ffffff,0xfffcf279}; draw.primitive={0,1,0,0};
            draw.otherMode={0x00553078,G_CYC_1CYCLE};
            sink->draw(draw);
            std::vector<uint8_t> actual;
            if(!sink->readFramebuffer(draw.colorAddress,8*8*4,actual))
                throw std::runtime_error("Zipper alpha framebuffer is missing");
            for(unsigned p=0;p<64;++p)
                if(actual[p*4]!=(alpha<32?0:255) || actual[p*4+1]!=0 || actual[p*4+2]!=(alpha<32?255:0))
                    throw std::runtime_error("Original zipper alpha coverage or untinted texture color changed");
        }
    }
}
static void checkCPUScanout(ProbeEGL &platform) {
    batching=true;
    std::vector<uint32_t> memory(0x20000/4);
    auto *rdram=reinterpret_cast<uint8_t *>(memory.data());
    auto sink=platform.createSink(); sink->setRDRAM(rdram,memory.size()*4);
    RT64::VI vi{};
    vi.width=8; vi.hRegion.word=(108U<<16)|124U;
    vi.vRegion.word=(37U<<16)|49U; vi.xTransform.word=512; vi.yTransform.word=1024;
    auto checkPixel=[&](int y,std::array<int,3> expected) {
        uint8_t rgba[4]{}; glReadPixels(480,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("CPU VI screenshot readback failed");
        for(unsigned c=0;c<3;++c) if(std::abs(int(rgba[c])-expected[c])>2)
            throw std::runtime_error("CPU-only VI framebuffer was not displayed correctly");
    };
    for(unsigned bytes:{2U,4U}) {
        const uint32_t base=0x400+bytes*0x100;
        for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
            const uint32_t at=base+(y*8+x)*bytes;
            if(bytes==2) { const uint16_t color=y<4?0xf801:0x003f; rdram[at^3]=color>>8; rdram[(at+1)^3]=color; }
            else { const uint8_t color[]={uint8_t(y<4?255:0),0,uint8_t(y<4?0:255),255}; for(unsigned c=0;c<4;++c) rdram[(at+c)^3]=color[c]; }
        }
        vi.status.word=bytes==2?2:3; vi.origin=base+vi.width*bytes;
        sink->present(vi); checkPixel(400,{255,0,0}); checkPixel(120,{0,0,255});
        if(sink->snapshotFramebuffer(base,8*8*bytes)) throw std::runtime_error("CPU scanout incorrectly claimed GPU ownership");
        std::vector<uint8_t> read;
        if(!sink->readFramebuffer(base+1,11,read) || read.size()!=11) throw std::runtime_error("CPU scanout readback failed");
        for(unsigned i=0;i<read.size();++i) if(read[i]!=rdram[(base+1+i)^3]) throw std::runtime_error("CPU scanout readback changed guest bytes");
        // A CPU edit must reach the next scanout without an RDP draw.
        for(unsigned i=0;i<8*8*bytes;++i) rdram[(base+i)^3]=0xff;
        sink->present(vi); checkPixel(400,{255,255,255}); checkPixel(120,{255,255,255});
    }
    // Many unrelated CPU screens must not exhaust the GPU framebuffer cache.
    vi.status.word=2;
    for(unsigned n=0;n<24;++n) { vi.origin=0x2000+n*0x100+16; sink->present(vi); checkPixel(272,{0,0,0}); }
    vi.origin=memory.size()*4+16; sink->present(vi); checkPixel(272,{0,0,0});
}
static void checkReadback(ProbeEGL &platform) {
    batching=true;
    auto sink=platform.createSink();
    RT64::FastDraw draw;
    draw.colorAddress=0x500000; draw.width=draw.height=4;
    draw.fill=true; draw.fillColor={1,0,0,1};
    quad(draw,-1,0,1,1,{1,1,1,1}); sink->draw(draw);
    draw.fillColor={0,0,1,1}; quad(draw,-1,-1,1,0,{1,1,1,1}); sink->draw(draw);
    std::vector<uint8_t> bytes;
    if(!sink->readFramebuffer(draw.colorAddress,32,bytes)) throw std::runtime_error("Resident framebuffer was not read back");
    for(unsigned i=0;i<16;++i) {
        const uint16_t expected=i<8?0xf801:0x003f;
        if(bytes[i*2]!=(expected>>8) || bytes[i*2+1]!=(expected&255))
            throw std::runtime_error("RGBA16 packing or framebuffer row order is wrong");
    }
    const auto full=bytes;
    if(!sink->readFramebuffer(draw.colorAddress+15,4,bytes) || bytes!=std::vector<uint8_t>(full.begin()+15,full.begin()+19))
        throw std::runtime_error("Unaligned framebuffer range read is wrong");
    const auto partial=bytes;
    if(sink->readFramebuffer(draw.colorAddress+31,2,bytes) || bytes!=partial)
        throw std::runtime_error("Out-of-range readback modified its destination");
    draw.colorBytes=4; draw.fillColor={0.25f,0.5f,0.75f,1};
    quad(draw,-1,-1,1,1,{1,1,1,1}); sink->draw(draw);
    if(!sink->readFramebuffer(draw.colorAddress,64,bytes)) throw std::runtime_error("RGBA32 framebuffer was not read back");
    for(unsigned i=0;i<16;++i) {
        const uint8_t expected[]={64,128,191,255};
        for(unsigned c=0;c<4;++c) if(bytes[i*4+c]!=expected[c])
            throw std::runtime_error("RGBA32 readback or color-format change is wrong");
    }
}
static void checkColorImageAliases(ProbeEGL &platform) {
    batching=true;
    for(uint32_t base:{0x400U,0x1800400U}) {
        std::vector<uint32_t> memory((base+0x1000)/4);
        auto *rdram=reinterpret_cast<uint8_t *>(memory.data());
        auto sink=platform.createSink(); sink->setRDRAM(rdram,memory.size()*4);
        std::vector<uint8_t> expected(512);
        RT64::FastDraw draw; draw.width=draw.height=8; draw.fill=true;
        draw.scissor={0,0,32,32};
        auto paint=[&](uint32_t address,unsigned left,unsigned top,unsigned right,unsigned bottom,uint16_t value) {
            draw.colorAddress=address;
            const auto expand=[](unsigned x){return float((x<<3)|(x>>2))/255;};
            draw.fillColor={expand(value>>11),expand((value>>6)&31),expand((value>>1)&31),float(value&1)};
            quad(draw,float(left)/4-1,1-float(bottom)/4,float(right)/4-1,1-float(top)/4,{1,1,1,1});
            sink->draw(draw);
            for(unsigned y=top;y<bottom;++y) for(unsigned x=left;x<right;++x) {
                const unsigned at=address-base+(y*8+x)*2;
                expected[at]=value>>8; expected[at+1]=value;
            }
        };
        auto checkRange=[&](uint32_t address,unsigned size) {
            std::vector<uint8_t> actual;
            const auto first=expected.begin()+address-base;
            if(!sink->readFramebuffer(address,size,actual) || actual!=std::vector<uint8_t>(first,first+size))
                throw std::runtime_error("Aliased color image lost GPU bytes at "+std::to_string(address));
        };
        paint(base,0,0,8,4,0xf801); paint(base,0,4,8,8,0x003f);
        auto old=sink->snapshotFramebuffer(base,128);
        const auto original=expected;
        paint(base+48,4,0,8,8,0x07c1);
        checkRange(base,128); checkRange(base+48,128);
        paint(base,0,0,4,8,0xffc1);
        checkRange(base+48,128); checkRange(base,128);
        std::vector<uint8_t> saved;
        if(!sink->readFramebufferSnapshot(*old,saved) || saved!=std::vector<uint8_t>(original.begin(),original.begin()+128))
            throw std::runtime_error("Aliased rendering altered an earlier snapshot");
        // An empty scissor changes the selected view without writing pixels.
        draw.scissor={0,0,0,0}; draw.colorAddress=base; draw.width=4; draw.height=16;
        sink->draw(draw); checkRange(base,128);
        auto reshaped=sink->snapshotFramebuffer(base,128);
        if(!reshaped || reshaped->width!=4 || reshaped->height!=16)
            throw std::runtime_error("Reshaped framebuffer view was not selected");
        draw.colorBytes=4; draw.height=8; sink->draw(draw); checkRange(base,128);
        draw.colorAddress=base+1; sink->draw(draw); checkRange(base+1,128);
        // Recorded writes must remain visible through every format/stride view.
        RT64::FastMemoryWrite write{base,1}; write.bytes[0]=0;
        rdram[base^3]=0; expected[0]=0; sink->notifyMemoryWrites({write});
        draw.colorAddress=base; draw.width=draw.height=8; draw.colorBytes=2;
        sink->draw(draw); checkRange(base,128);
        rdram[(base+1)^3]=0xf8; expected[1]=0xf8;
        checkRange(base,128); checkRange(base+48,128);
        // Repeated interpretations must retire redundant views without losing
        // the only copy of bytes outside the smaller interpretations.
        for(unsigned width=64;width>=40;--width) {
            draw.colorAddress=base; draw.width=width; draw.height=1;
            sink->draw(draw); checkRange(base,128);
        }
        draw.width=draw.height=8; sink->draw(draw); checkRange(base,128);
        if(!sink->readFramebufferSnapshot(*old,saved) || saved!=std::vector<uint8_t>(original.begin(),original.begin()+128))
            throw std::runtime_error("Retiring alias views invalidated an immutable snapshot");
        // Once CPU stores replace the whole range, loads must use RAM directly
        // instead of forcing a GPU readback for an ordinary reused texture.
        std::vector<RT64::FastMemoryWrite> writes(4);
        for(unsigned w=0;w<4;++w) {
            writes[w].address=base+w*32; writes[w].mask=UINT32_MAX;
            for(unsigned i=0;i<32;++i) {
                const uint8_t value=uint8_t(w*32+i); writes[w].bytes[i]=value;
                rdram[(base+w*32+i)^3]=value; expected[w*32+i]=value;
            }
        }
        sink->notifyMemoryWrites(writes); checkRange(base,128);
        if(sink->snapshotFramebuffer(base,128)) throw std::runtime_error("CPU-owned texture range still requires a GPU snapshot");
    }
}
static void checkColorImageViewRetirement(ProbeEGL &platform) {
    batching=true;
    std::vector<uint32_t> memory(0x2000/4);
    auto sink=platform.createSink();
    sink->setRDRAM(reinterpret_cast<uint8_t *>(memory.data()),memory.size()*4);
    constexpr uint32_t base=0x400;
    RT64::FastDraw draw; draw.colorAddress=base; draw.width=draw.height=8;
    draw.fill=true; draw.fillColor={1,0,0,1}; draw.scissor={0,0,32,32};
    quad(draw,-1,-1,1,1,{1,1,1,1}); sink->draw(draw);
    // Two newer halves collectively cover the original image, but neither
    // provides the full range required by a later read or texture snapshot.
    draw.vertices.clear(); draw.scissor={0,0,0,0}; draw.height=4;
    sink->draw(draw); draw.colorAddress=base+64; sink->draw(draw);
    draw.width=draw.height=1;
    for(unsigned i=0;i<20;++i) {
        draw.colorAddress=base+2+i*2; sink->draw(draw);
    }
    std::vector<uint8_t> bytes;
    if(!sink->readFramebuffer(base,128,bytes) || bytes.size()!=128)
        throw std::runtime_error("Retiring split aliases lost the full framebuffer view");
    for(unsigned i=0;i<128;i+=2) if(bytes[i]!=0xf8 || bytes[i+1]!=1)
        throw std::runtime_error("Consolidating split aliases changed framebuffer bytes");
    if(!sink->snapshotFramebuffer(base,128))
        throw std::runtime_error("Retiring split aliases lost the full framebuffer snapshot");
}
static void checkDepthImageSharing(ProbeEGL &platform) {
    batching=true;
    std::vector<uint32_t> memory(0x4000/4);
    auto sink=platform.createSink();
    sink->setRDRAM(reinterpret_cast<uint8_t *>(memory.data()),memory.size()*4);
    constexpr uint32_t a=0x1000,b=0x1800,z1=0x2000,z2=0x2800;
    RT64::FastDraw draw;
    draw.width=draw.height=8;
    draw.scissor={0,0,32,32};
    draw.combine={0x00ffffff,(15U<<24)|(7U<<21)|(7U<<18)|(4U<<6)|(7U<<3)|4U};
    draw.otherMode.H=G_CYC_1CYCLE;
    auto paint=[&](uint32_t color,uint32_t depth,float z,float left,float right,std::array<float,4> rgba,bool useDepth=true) {
        draw.colorAddress=color; draw.depthAddress=depth;
        draw.depthTest=draw.depthWrite=useDepth;
        draw.otherMode.L=useDepth?Z_CMP|Z_UPD:0;
        quad(draw,left,-1,right,1,rgba);
        for(auto &v:draw.vertices) v.position[2]=z;
        sink->draw(draw);
    };
    auto expect=[&](uint32_t address,uint16_t left,uint16_t right) {
        std::vector<uint8_t> bytes;
        if(!sink->readFramebuffer(address,128,bytes)) throw std::runtime_error("Depth sharing color image is missing");
        for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
            const unsigned pixel=(y*8+x)*2,value=x<4?left:right;
            if(bytes[pixel]!=(value>>8) || bytes[pixel+1]!=(value&255))
                throw std::runtime_error("Color/depth image switch lost color or depth contents at "+std::to_string(address));
        }
    };
    paint(b,z1,0,-1,1,{1,1,0,1},false);
    paint(a,z1,-0.5f,-1,1,{1,0,0,1});
    sink->fullSync(); sink->present(a);
    paint(a,z2,0.5f,0,1,{0,0,1,1});
    expect(a,0xf801,0x003f); // Changing Z image must preserve the left half's red.
    paint(a,z1,0,-1,1,{0,1,0,1});
    expect(a,0xf801,0x003f); // Z1's nearer depth survives switching away/back.
    paint(b,z1,0.5f,-1,1,{0,0,1,1});
    expect(b,0xffc1,0xffc1); // B sees A's depth, so the farther blue is rejected.
    RT64::FastDraw clear;
    clear.colorAddress=clear.depthAddress=z1; clear.width=clear.height=8;
    clear.clearDepth=clear.fill=true; clear.scissor={0,0,16,32};
    quad(clear,-1,-1,0,1,{1,1,1,1}); sink->draw(clear);
    paint(b,z1,0,-1,1,{1,0,1,1}); expect(b,0xf83f,0xffc1);
    // Updating A's color attachment through a CPU write must not reset its Z1.
    RT64::FastMemoryWrite write{a,2}; write.bytes[1]=0x3f;
    reinterpret_cast<uint8_t *>(memory.data())[(a+1)^3]=0x3f;
    sink->notifyMemoryWrites({write});
    std::vector<uint8_t> before,after;
    sink->readFramebuffer(a,128,before);
    paint(a,z1,0.5f,-1,1,{0,0,1,1}); sink->readFramebuffer(a,128,after);
    if(before!=after) throw std::runtime_error("CPU color update reset shared depth");
    // Replacing a color allocation must detach it without destroying shared Z.
    std::fill_n(reinterpret_cast<uint8_t *>(memory.data())+a,256,255);
    draw.colorBytes=4; paint(a,z1,0.5f,-1,1,{0,0,1,1});
    if(!sink->readFramebuffer(a,256,after) || after!=std::vector<uint8_t>(256,255))
        throw std::runtime_error("Color format replacement destroyed shared depth");
}
static void checkFramebufferFeedback(ProbeEGL &platform) {
    batching=true;
    auto sink=platform.createSink();
    std::vector<uint32_t> memory(2*1024*1024);
    RT64::State state(reinterpret_cast<uint8_t *>(memory.data()),memory.size()*4,*sink);
    RT64::FastDraw draw;
    draw.colorAddress=0x600000; draw.width=draw.height=8; draw.fill=true;
    draw.fillColor={1,0,0,1}; quad(draw,-1,0,1,1,{1,1,1,1}); sink->draw(draw);
    draw.fillColor={0,0,1,1}; quad(draw,-1,-1,1,0,{1,1,1,1}); sink->draw(draw);
    auto &rdp=*state.rdp;
    rdp.setTextureImage(G_IM_FMT_RGBA,G_IM_SIZ_16b,8,draw.colorAddress);
    rdp.setTile(7,G_IM_FMT_RGBA,G_IM_SIZ_16b,1,0,0,G_TX_CLAMP,G_TX_CLAMP,0,0,0,0);
    rdp.loadTile(7,8,4,20,24); // Four columns, six rows; a subview crosses the color boundary.
    rdp.setTile(0,G_IM_FMT_RGBA,G_IM_SIZ_16b,1,0,0,G_TX_CLAMP,G_TX_CLAMP,0,0,0,0);
    rdp.setTileSize(0,0,0,12,20);
    auto loaded=rdp.decodeTexture(0);
    if(!loaded->storage || loaded->width!=4 || loaded->height!=6 || loaded->storageX!=2 || loaded->storageY!=1)
        throw std::runtime_error("Framebuffer load/render tile mapping is incorrect");
    // Repainting the source after the load must not alter the loaded texture.
    draw.fillColor={0,1,0,1}; quad(draw,-1,-1,1,1,{1,1,1,1}); sink->draw(draw);
    draw.colorAddress=0x610000; draw.fill=false; draw.textures[0]=loaded;
    draw.tiles[0].cms=draw.tiles[0].cmt=G_TX_CLAMP;
    draw.tiles[0].lrs=12; draw.tiles[0].lrt=20;
    draw.otherMode={0,G_CYC_COPY};
    quad(draw,-1,-1,1,1,{1,1,1,1});
    for(auto &v:draw.vertices) { v.uv[0]=(v.position[0]+1)*2; v.uv[1]=(1-v.position[1])*3; }
    sink->draw(draw); sink->fullSync();
    std::vector<uint8_t> output;
    if(!sink->readFramebuffer(draw.colorAddress,128,output)) throw std::runtime_error("Feedback output is missing");
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        const unsigned expected=y<4?0xf801:0x003f;
        const unsigned at=(y*8+x)*2;
        if(output[at]!=(expected>>8) || output[at+1]!=(expected&255))
            throw std::runtime_error("Framebuffer feedback changed after load or has incorrect orientation");
    }
    // Reusing the old framebuffer memory as ordinary texture data must not
    // return the cached green framebuffer instead of the new CPU pixels.
    for(unsigned i=0;i<64;++i) {
        state.RDRAM[(0x600000+i*2)^3]=0xff;
        state.RDRAM[(0x600001+i*2)^3]=0xff;
    }
    rdp.setTextureImage(G_IM_FMT_RGBA,G_IM_SIZ_16b,8,0x600000);
    rdp.loadTile(7,0,0,12,0); rdp.setTileSize(0,0,0,12,0);
    auto reused=rdp.decodeTexture(0);
    draw.colorAddress=0x620000; draw.textures[0]=reused; draw.tiles[0].lrt=0;
    for(auto &v:draw.vertices) v.uv[1]=0.5f;
    sink->draw(draw);
    if(!sink->readFramebuffer(draw.colorAddress,128,output) || output!=std::vector<uint8_t>(128,255))
        throw std::runtime_error("CPU texture data was replaced by a stale framebuffer");
}
static void checkFramebufferMemoryChanges(ProbeEGL &platform) {
    batching=true;
    for(unsigned bpp:{2U,4U}) {
        auto sink=platform.createSink();
        std::vector<uint8_t> memory(8192,0xaa);
        sink->setRDRAM(memory.data(),memory.size());
        RT64::FastDraw draw;
        draw.colorAddress=0x402; draw.width=draw.height=4; draw.colorBytes=bpp;
        draw.memoryEpoch=1; draw.fill=true; draw.fillColor={0.25f,0.5f,0.75f,1};
        quad(draw,-1,-1,1,1,{1,1,1,1}); sink->draw(draw);
        std::vector<uint8_t> expected,output;
        if(!sink->readFramebuffer(draw.colorAddress,16*bpp,expected)) throw std::runtime_error("Missing CPU merge test image");
        const auto before=expected;
        auto old=sink->snapshotFramebuffer(draw.colorAddress,16*bpp);
        auto change=[&](unsigned at,uint8_t byte) { memory[(draw.colorAddress+at)^3]=byte; expected[at]=byte; };
        // Single-byte writes include half of an RGBA16 green channel and RGBA32
        // alpha. Other bytes must keep the GPU result, not the stale 0xaa RAM.
        change(1,0); change(3*bpp,0x98); change(14*bpp,0xf8); change(14*bpp+1,1);
        if(bpp==4) change(7,0);
        if(!sink->readFramebuffer(draw.colorAddress,16*bpp,output) || output!=expected)
            throw std::runtime_error("Changed RAM bytes did not merge with preserved framebuffer bytes ("+std::to_string(bpp)+")");
        if(!sink->readFramebufferSnapshot(*old,output) || output!=before)
            throw std::runtime_error("RAM changes altered an already loaded framebuffer snapshot");
        // A subsequent graphics task must ingest RAM changes before drawing;
        // merely refreshing the RAM shadow would lose the CPU update.
        change(0,0x27); ++draw.memoryEpoch;
        draw.fillColor={1,1,0,1}; quad(draw,0,-1,1,0,{1,1,1,1}); sink->draw(draw);
        const uint8_t yellow16[]={0xff,0xc1},yellow32[]={255,255,0,255};
        for(unsigned y=2;y<4;++y) for(unsigned x=2;x<4;++x)
            std::copy_n(bpp==2?yellow16:yellow32,bpp,expected.begin()+(y*4+x)*bpp);
        if(!sink->readFramebuffer(draw.colorAddress,16*bpp,output) || output!=expected)
            throw std::runtime_error("New graphics task lost CPU changes or restored stale RAM over GPU output");
    }
}
static void checkRecordedFramebufferWrites(ProbeEGL &platform) {
    batching=true;
    for(unsigned bpp:{2U,4U}) {
        std::vector<uint32_t> memory(2048,0xaaaaaaaa);
        auto *rdram=reinterpret_cast<uint8_t *>(memory.data());
        recomp::initialize_memory_writes(rdram,memory.size()*4);
        auto sink=platform.createSink();
        sink->setRDRAM(rdram,memory.size()*4); track_framebuffer_writes(*sink);
        RT64::FastDraw draw;
        draw.colorAddress=0x400; draw.width=draw.height=4; draw.colorBytes=bpp;
        draw.fill=true; draw.fillColor={0,1,0,1};
        quad(draw,-1,-1,1,1,{1,1,1,1}); sink->draw(draw); sink->flushDraws();
        std::vector<uint8_t> before,output;
        if(!sink->readFramebuffer(draw.colorAddress,16*bpp,before)) throw std::runtime_error("Tracked framebuffer is missing");
        auto old=sink->snapshotFramebuffer(draw.colorAddress,16*bpp);
        if(bpp==2) do_sb(rdram,1,0xffffffffa0000400ULL,0xaa);
        else do_sw(rdram,0,0xffffffff80000400ULL,0xaaaaaaaa);
        if(!sink->readFramebuffer(draw.colorAddress,16*bpp,output) || output!=before)
            throw std::runtime_error("Same-value write negative control changed RAM unexpectedly");
        submit_framebuffer_writes(*sink);
        auto expected=before;
        if(bpp==2) expected[1]=0xaa;
        else std::fill_n(expected.begin(),4,0xaa);
        if(!sink->readFramebuffer(draw.colorAddress,16*bpp,output) || output!=expected)
            throw std::runtime_error("Recorded same-value CPU store was lost by the GPU framebuffer");
        if(!sink->readFramebufferSnapshot(*old,output) || output!=before)
            throw std::runtime_error("CPU store changed a snapshot loaded before that store");
        sink.reset();
        if(__atomic_load_n(&recomp_watched_pages[0],__ATOMIC_RELAXED))
            throw std::runtime_error("Destroyed framebuffer left guest pages watched");
    }
}
int main() {
    try {
        auto platform=createProbeEGL(".");
        const auto reference=render(*platform,false);
        const auto merged=render(*platform,true);
        if(reference!=merged) throw std::runtime_error("Batched pixels differ from unbatched pixels");
        const size_t center=(272*960+480)*4;
        const int expected[]={72,16,151};
        for(unsigned i=0;i<3;++i)
            if(std::abs(int(merged[center+i])-expected[i])>2)
                throw std::runtime_error("Translucent primitive order produced the wrong center color");
        for(unsigned side=0;side<2;++side) {
            const size_t pixel=(430*960+(side?660:300))*4;
            const int red=side?64:191,green=side?191:64;
            if(std::abs(int(merged[pixel])-red)>2 || std::abs(int(merged[pixel+1])-green)>2 || merged[pixel+2]>2)
                throw std::runtime_error("Texture-unit binding or cached LOD uniform update is incorrect");
        }
        checkPresentation(*platform);
        checkUniformStateChanges(*platform);
        checkTextureUniformStateChanges(*platform);
        checkZipperTextureAlpha(*platform);
        checkCPUScanout(*platform);
        checkReadback(*platform);
        checkColorImageAliases(*platform);
        checkColorImageViewRetirement(*platform);
        checkDepthImageSharing(*platform);
        checkFramebufferMemoryChanges(*platform);
        checkRecordedFramebufferWrites(*platform);
        checkFramebufferFeedback(*platform);
        std::puts("GL checks: batching, translucency, textures, zipper alpha, VI, readback, recorded writes, shared depth and color-buffer aliases passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
