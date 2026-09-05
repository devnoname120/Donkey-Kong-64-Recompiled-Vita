#include "fast/rt64_fast_interpreter.h"
#include "gbi/rt64_gbi_f3dex2.h"
#include "gbi/rt64_gbi_rdp.h"
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/gxm.h>
#include <vitaGL.h>
#include <cstdio>
#include <exception>
#include <vector>
#include <cstring>

int _newlib_heap_size_user = 128 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;

static void trace(const char *message) {
    SceUID fd=sceIoOpen("ux0:data/rt64-fast/progress.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0777);
    if(fd>=0) { sceIoWrite(fd,message,std::strlen(message)); sceIoWrite(fd,"\n",1); sceIoClose(fd); }
}

static void capture() {
    std::vector<uint8_t> rgba(320*240*4), rgb(320*240*3);
    glFinish();
    glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Renderer smoke readback failed");
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) for(unsigned c=0;c<3;++c)
        rgb[(y*320+x)*3+c]=rgba[((239-y)*320+x)*4+c];
    const char header[]="P6\n320 240\n255\n";
    SceUID fd=sceIoOpen("ux0:data/rt64-fast/frame.ppm",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
    if(fd<0) throw std::runtime_error("Renderer smoke could not open readback output");
    sceIoWrite(fd,header,sizeof(header)-1); sceIoWrite(fd,rgb.data(),rgb.size()); sceIoClose(fd);
    trace("Read back 320x240 offscreen framebuffer to frame.ppm");
}

int main() {
    sceIoMkdir("ux0:data/rt64-fast",0777);
    std::freopen("ux0:data/rt64-fast/smoke.log","w",stdout);
    std::freopen("ux0:data/rt64-fast/error.log","w",stderr);
    std::setvbuf(stdout,nullptr,_IOLBF,0);
    std::setvbuf(stderr,nullptr,_IOLBF,0);
    trace("RT64 Fast smoke: starting (vitaGL NO_SPLASHSCREEN=1 required)");
    try {
        auto sink=RT64::createFastVitaGLSink(true,false);
        trace("vitaGL and shader compiler initialized");
        std::vector<uint32_t> rdram(2*1024*1024);
        RT64::State state(reinterpret_cast<uint8_t *>(rdram.data()),rdram.size()*4,*sink);
        RT64::GBI gbi;
        gbi.ucode=RT64::GBIUCode::F3DEX2;
        RT64::GBI_RDP::setup(&gbi,true); RT64::GBI_F3DEX2::setup(&gbi);
        RT64::Interpreter interpreter;
        interpreter.setup(&state); interpreter.hleGBI=&gbi; state.rsp->setGBI(&gbi);
        auto command=[&](unsigned i,uint32_t a,uint32_t b){rdram[0x100/4+i*2]=a;rdram[0x100/4+i*2+1]=b;};
        command(0,0xff10013f,0x100000); // RGBA16, width 320
        command(1,0xfe000000,0x200000);
        command(2,0xed000000,0x005003c0); // 320x240 scissor
        command(3,0xef300000,0); // fill cycle
        command(4,0xf7000000,0x10851085); // dark background
        command(5,0xf64fc3bc,0); // full-screen inclusive fill
        command(6,0xdf000000,0);
        // A directional RGBA pattern exposes swapped channels, bad UV bindings,
        // texture stride errors and incorrect repeat/mirror sampling.
        auto texture=std::make_shared<RT64::FastTexture>();
        texture->width=texture->height=64; texture->hash=0x74657874757265;
        texture->rgba.resize(64*64*4);
        for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
            auto *p=&texture->rgba[(y*64+x)*4];
            p[0]=x*4; p[1]=y*4; p[2]=((x/8)^(y/8))&1?255:0; p[3]=255;
        }
        auto secondTexture=std::make_shared<RT64::FastTexture>(*texture);
        secondTexture->hash+=1;
        for(size_t i=0;i<secondTexture->rgba.size();i+=4)
            for(unsigned c=0;c<3;++c) secondTexture->rgba[i+c]=255-secondTexture->rgba[i+c];
        RT64::FastDraw textured;
        textured.colorAddress=0x100000; textured.depthAddress=0x200000;
        textured.textures[0]=texture;
        textured.tiles[0].lrs=textured.tiles[0].lrt=63*4;
        textured.tiles[0].masks=textured.tiles[0].maskt=6;
        textured.vertices.resize(6);
        const unsigned corners[6]={0,1,2,0,2,3};
        const float quad[4][2]={{0,0},{1,0},{1,1},{0,1}};
        for(unsigned frame=0;;++frame) {
            interpreter.processDisplayLists(0x100,reinterpret_cast<RT64::DisplayList *>(state.fromRDRAM(0x100)));
            RT64::FastDraw triangle;
            triangle.colorAddress=0x100000; triangle.depthAddress=0x200000;
            // (0-0)*0 + SHADE in the second color and alpha muxes.
            triangle.combine={0x00ffffff,0xfffcf279};
            triangle.combine.H=(15U<<24)|(7U<<21)|(7U<<18)|(4U<<6)|(7U<<3)|4U;
            triangle.vertices.resize(3);
            const float xy[3][2]={{-0.9f,0.05f},{-0.1f,0.05f},{-0.5f,0.9f}};
            for(unsigned i=0;i<3;++i) {
                triangle.vertices[i].position[0]=xy[i][0]; triangle.vertices[i].position[1]=xy[i][1];
                for(unsigned c=0;c<3;++c) triangle.vertices[i].color[c]=i==c?1:0;
            }
            sink->draw(triangle);
            for(unsigned mode=0;mode<3;++mode) {
                textured.otherMode.H=mode==1?0x00992c00:G_CYC_COPY;
                textured.otherMode.L=mode==1?0x0c192078:0;
                textured.combine={0xfc26a004,0x1f1093ff};
                textured.textures[1]=mode==1?secondTexture:nullptr;
                textured.tiles[0].cms=textured.tiles[0].cmt=mode==1?G_TX_MIRROR:G_TX_WRAP;
                textured.tiles[0].masks=textured.tiles[0].maskt=6;
                if(mode>=2) {
                    textured.otherMode={0x00552230,0x0088ac00};
                    textured.combine={0xfc127e24,0xfffff9fc};
                    textured.textures[1].reset();
                    textured.tiles[0].cms=textured.tiles[0].cmt=G_TX_CLAMP;
                    textured.tiles[0].masks=textured.tiles[0].maskt=0;
                }
                textured.tiles[1]=textured.tiles[0];
                for(unsigned i=0;i<6;++i) {
                    auto &v=textured.vertices[i]; const auto &p=quad[corners[i]];
                    v.position[0]=(mode==2?-0.9f:0.05f)+p[0]*0.85f;
                    v.position[1]=(mode?-0.9f:0.05f)+p[1]*0.85f;
                    v.uv[0]=p[0]*128-32; v.uv[1]=p[1]*128-32;
                }
                sink->draw(textured);
            }
            sink->fullSync();
            if(frame==2) capture();
            sink->present(0x100000);
            if(frame==0) trace("First GBI fill and RGB triangle presented");
            if(frame==119) trace("120 frames presented successfully");
            SceCtrlData pad{}; sceCtrlPeekBufferPositive(0,&pad,1);
            if(pad.buttons&SCE_CTRL_CIRCLE) break;
        }
        trace("RT64 Fast smoke completed");
    } catch(const std::exception &e) {
        std::fprintf(stderr,"RT64 Fast smoke failed: %s\n",e.what());
        std::fprintf(stdout,"RT64 Fast smoke failed: %s\n",e.what());
        trace(e.what());
        return 1;
    }
    return 0;
}
