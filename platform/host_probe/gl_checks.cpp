#include "egl.h"
#include "hle/rt64_vi.h"
#include <GLES2/gl2.h>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

static bool batching=false;
unsigned probeCompletedTasks() { return 0; }
bool probeBatchingEnabled() { return batching; }

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
        checkReadback(*platform);
        std::puts("GL checks: exact batched/unbatched pixels; translucent order, textures, uniforms, VI and RGBA16/32 range readback passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
