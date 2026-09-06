#include "fast/rt64_fast_interpreter.h"
#include "gbi/rt64_gbi_f3dex2.h"
#include "gbi/rt64_gbi_rdp.h"
#include "hle/rt64_vi.h"
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
#include <algorithm>
#include <cstring>
#include "memory_writes.h"
#ifdef RECOMP_TRACK_MEMORY_WRITES
#include "recomp.h"
#endif

int _newlib_heap_size_user = 128 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;

static void trace(const char *message) {
    SceUID fd=sceIoOpen("ux0:data/rt64-fast/progress.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0777);
    if(fd>=0) { sceIoWrite(fd,message,std::strlen(message)); sceIoWrite(fd,"\n",1); sceIoClose(fd); }
}

static void capture(unsigned frame) {
    std::vector<uint8_t> rgba(320*240*4), rgb(320*240*3);
    glFinish();
    glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Renderer smoke readback failed");
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) for(unsigned c=0;c<3;++c)
        rgb[(y*320+x)*3+c]=rgba[((239-y)*320+x)*4+c];
    const char header[]="P6\n320 240\n255\n";
    char path[96];
    if(frame==2) std::snprintf(path,sizeof(path),"ux0:data/rt64-fast/frame.ppm");
    else std::snprintf(path,sizeof(path),"ux0:data/rt64-fast/frame-%u.ppm",frame);
    SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
    if(fd<0) throw std::runtime_error("Renderer smoke could not open readback output");
    sceIoWrite(fd,header,sizeof(header)-1); sceIoWrite(fd,rgb.data(),rgb.size()); sceIoClose(fd);
    unsigned colored=0;
    for(size_t i=0;i<rgb.size();i+=3) colored+=rgb[i] || rgb[i+1] || rgb[i+2];
    char message[160];
    std::snprintf(message,sizeof(message),"Read back frame %u to %s: %u/76800 non-black pixels",frame,path,colored);
    trace(message);
}

static void checkGPUReadback(RT64::FastDrawSink &sink,unsigned sequence) {
    // Alternate known GPU-only images. Repeating a static image cannot expose
    // readback of the previous frame, and CPU-owned images bypass this path.
    constexpr uint32_t address=0x300000, width=64, height=64;
    RT64::FastDraw draw;
    draw.colorAddress=address; draw.width=width; draw.height=height;
    draw.fill=true; draw.vertices.resize(6);
    const unsigned corners[6]={0,1,2,0,2,3};
    const float xy[4][2]={{0,0},{1,0},{1,1},{0,1}};
    const std::array<float,4> colors[2][2]={
        {{1,0,0,1},{0,0,1,1}}, {{0,1,0,1},{1,1,0,1}}
    };
    const uint16_t packed[2][2]={{0xf801,0x003f},{0x07c1,0xffc1}};
    for(unsigned half=0;half<2;++half) {
        draw.fillColor=colors[sequence&1][half];
        for(unsigned i=0;i<6;++i) {
            const auto &p=xy[corners[i]];
            draw.vertices[i].position[0]=p[0]*2-1;
            draw.vertices[i].position[1]=p[1]-half;
        }
        sink.draw(draw);
    }
    sink.fullSync();
    std::vector<uint8_t> bytes;
    if(!sink.readFramebuffer(address,width*height*2,bytes) || bytes.size()!=width*height*2)
        throw std::runtime_error("GPU-owned framebuffer readback failed");
    unsigned current=0,previous=0,zero=0;
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const unsigned offset=(y*width+x)*2;
        const uint16_t value=(uint16_t(bytes[offset])<<8)|bytes[offset+1];
        current+=value==packed[sequence&1][y>=height/2];
        previous+=value==packed[(sequence^1)&1][y>=height/2];
        zero+=value==0;
    }
    char path[96],message[192];
    std::snprintf(path,sizeof(path),"ux0:data/rt64-fast/gpu-readback-%u.rgba16",sequence);
    SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
    if(fd<0) throw std::runtime_error("Could not open GPU readback output");
    sceIoWrite(fd,bytes.data(),bytes.size()); sceIoClose(fd);
    std::snprintf(message,sizeof(message),
        "GPU readback %u: %u/4096 current, %u/4096 previous, %u/4096 zero pixels",
        sequence,current,previous,zero);
    trace(message);
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
#ifdef RECOMP_TRACK_MEMORY_WRITES
        recomp::initialize_memory_writes(reinterpret_cast<uint8_t *>(rdram.data()),rdram.size()*4);
#endif
        RT64::State state(reinterpret_cast<uint8_t *>(rdram.data()),rdram.size()*4,*sink);
        track_framebuffer_writes(*sink);
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
        RT64::FastDraw source;
        source.colorAddress=0x180000; source.width=source.height=8; source.fill=true;
        source.vertices.resize(6);
        auto sourceQuad=[&](float bottom,float top,std::array<float,4> color) {
            source.fillColor=color;
            for(unsigned i=0;i<6;++i) {
                const auto &p=quad[corners[i]];
                source.vertices[i].position[0]=p[0]*2-1;
                source.vertices[i].position[1]=bottom+p[1]*(top-bottom);
            }
            sink->draw(source);
        };
        sourceQuad(0,1,{1,0,0,1}); sourceQuad(-1,0,{0,0,1,1});
        state.rdp->setTextureImage(G_IM_FMT_RGBA,G_IM_SIZ_16b,8,source.colorAddress);
        state.rdp->setTile(7,G_IM_FMT_RGBA,G_IM_SIZ_16b,1,0,0,G_TX_CLAMP,G_TX_CLAMP,0,0,0,0);
        state.rdp->loadTile(7,8,4,20,24);
        state.rdp->setTile(0,G_IM_FMT_RGBA,G_IM_SIZ_16b,1,0,0,G_TX_CLAMP,G_TX_CLAMP,0,0,0,0);
        state.rdp->setTileSize(0,0,0,12,20);
        auto feedback=state.rdp->decodeTexture(0);
        if(!feedback->storage) throw std::runtime_error("Framebuffer feedback did not produce a GPU view");
        sourceQuad(-1,1,{0,1,1,1});
        trace("Framebuffer snapshot captured red/blue; source repainted cyan");
        // Store zero over RAM's existing zero high byte in the lower half.
        // The GPU's cyan 0x07ff must become 0x00ff: RGB (0,24,255).
        std::vector<RT64::FastMemoryWrite> sourceWrites(4);
        for(unsigned i=0;i<4;++i) sourceWrites[i].address=source.colorAddress+i*32;
        auto writeByte=[&](uint32_t at,uint8_t value) {
#ifdef RECOMP_TRACK_MEMORY_WRITES
            do_sb(state.RDRAM,0,at,value);
#else
            state.RDRAM[at^3]=value;
#endif
            const unsigned offset=at-source.colorAddress;
            auto &write=sourceWrites[offset/32];
            write.mask|=1U<<(offset&31); write.bytes[offset&31]=value;
        };
        for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
            const uint32_t at=source.colorAddress+(y*8+x)*2;
            writeByte(at,y<4?0xf8:0);
            if(y<4) writeByte(at+1,1);
        }
#ifdef RECOMP_TRACK_MEMORY_WRITES
        submit_framebuffer_writes(*sink);
#else
        sink->notifyMemoryWrites(sourceWrites);
#endif
        state.rdp->setTile(7,G_IM_FMT_RGBA,G_IM_SIZ_16b,2,0,0,G_TX_CLAMP,G_TX_CLAMP,0,0,0,0);
        state.rdp->loadTile(7,0,0,28,28);
        state.rdp->setTile(0,G_IM_FMT_RGBA,G_IM_SIZ_16b,2,0,0,G_TX_CLAMP,G_TX_CLAMP,0,0,0,0);
        state.rdp->setTileSize(0,0,0,28,28);
        auto changedFeedback=state.rdp->decodeTexture(0);
        if(!changedFeedback->storage) throw std::runtime_error("Framebuffer merge diagnostic requires a GPU texture view");
        trace("Merged recorded CPU stores into GPU image; expected red above blue (0,24,255)");
        RT64::FastDraw depthDraw;
        depthDraw.width=depthDraw.height=8; depthDraw.scissor={0,0,32,32};
        depthDraw.combine={0x00ffffff,(15U<<24)|(7U<<21)|(7U<<18)|(4U<<6)|(7U<<3)|4U};
        depthDraw.otherMode.H=G_CYC_1CYCLE; depthDraw.vertices.resize(6);
        auto depthQuad=[&](uint32_t color,uint32_t depth,float z,float left,float right,std::array<float,4> rgba,bool enabled=true) {
            depthDraw.colorAddress=color; depthDraw.depthAddress=depth;
            depthDraw.depthTest=depthDraw.depthWrite=enabled;
            depthDraw.otherMode.L=enabled?Z_CMP|Z_UPD:0;
            for(unsigned i=0;i<6;++i) {
                auto &v=depthDraw.vertices[i]; const auto &p=quad[corners[i]];
                v.position[0]=left+(right-left)*p[0]; v.position[1]=p[1]*2-1; v.position[2]=z;
                std::copy(rgba.begin(),rgba.end(),v.color);
            }
            sink->draw(depthDraw);
        };
        constexpr uint32_t colorA=0x1a0000,colorB=0x1b0000,depthA=0x1c0000,depthB=0x1d0000;
        depthQuad(colorB,depthA,0,-1,1,{1,1,0,1},false);
        depthQuad(colorA,depthA,-0.5f,-1,1,{1,0,0,1});
        sink->fullSync(); sink->present(colorA);
        depthQuad(colorA,depthB,0.5f,0,1,{0,0,1,1});
        depthQuad(colorA,depthA,0,-1,1,{0,1,0,1});
        depthQuad(colorB,depthA,0.5f,-1,1,{0,0,1,1});
        RT64::FastDraw depthClear=depthDraw;
        depthClear.colorAddress=depthA; depthClear.clearDepth=depthClear.fill=true;
        depthClear.scissor={0,0,16,32}; sink->draw(depthClear);
        depthQuad(colorB,depthA,0,-1,1,{1,0,1,1});
        RT64::FastMemoryWrite sameBlue{colorA,1U<<15}; sameBlue.bytes[15]=0x3f;
        state.RDRAM[(colorA+15)^3]=0x3f;
        sink->notifyMemoryWrites({sameBlue});
        depthQuad(colorA,depthA,0.5f,-1,1,{0,0,1,1});
        auto depthImageA=sink->snapshotFramebuffer(colorA,128),depthImageB=sink->snapshotFramebuffer(colorB,128);
        if(!depthImageA || !depthImageB) throw std::runtime_error("Shared depth diagnostic is missing its color images");
        trace("Shared depth test prepared: red/blue above magenta/yellow");
        constexpr uint32_t aliasAddress=0x260000;
        RT64::FastDraw aliasDraw;
        aliasDraw.colorAddress=aliasAddress; aliasDraw.width=aliasDraw.height=8;
        aliasDraw.fill=true; aliasDraw.scissor={0,0,32,32}; aliasDraw.vertices.resize(6);
        auto aliasQuad=[&](float left,float bottom,float right,float top,std::array<float,4> color) {
            aliasDraw.fillColor=color;
            for(unsigned i=0;i<6;++i) {
                const auto &p=quad[corners[i]]; auto &v=aliasDraw.vertices[i];
                v.position[0]=left+(right-left)*p[0]; v.position[1]=bottom+(top-bottom)*p[1];
            }
            sink->draw(aliasDraw);
        };
        aliasQuad(-1,0,1,1,{1,0,0,1}); aliasQuad(-1,-1,1,0,{0,0,1,1});
        auto aliasBefore=sink->snapshotFramebuffer(aliasAddress,128);
        aliasDraw.colorAddress=aliasAddress+48;
        aliasQuad(0,-1,1,1,{0,1,0,1});
        // Reinterpret the same bytes as RGBA32, then return to the original
        // RGBA16 layout without drawing over the result.
        aliasDraw.colorAddress=aliasAddress; aliasDraw.colorBytes=4; aliasDraw.height=4;
        aliasDraw.scissor={0,0,0,0}; sink->draw(aliasDraw);
        aliasDraw.colorBytes=2; aliasDraw.height=8; sink->draw(aliasDraw);
        auto aliasAfter=sink->snapshotFramebuffer(aliasAddress,128);
        if(!aliasBefore || !aliasAfter) throw std::runtime_error("Color alias diagnostic is missing its GPU snapshots");
        trace("Color alias test prepared: original red/blue beside merged red/blue/green");
        constexpr uint32_t cpuAddress=0x2e0000;
        RT64::VI cpuVI{};
        cpuVI.status.word=3; cpuVI.width=8; cpuVI.origin=cpuAddress+32;
        cpuVI.hRegion.word=(108U<<16)|124U; cpuVI.vRegion.word=(37U<<16)|49U;
        cpuVI.xTransform.word=512; cpuVI.yTransform.word=1024;
        auto writeCPUImage=[&](bool changed) {
            for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
                const uint8_t color[4]={uint8_t(y<4 || changed?255:0),uint8_t(y<4 || !changed?255:0),uint8_t(y<4?0:255),255};
                for(unsigned c=0;c<4;++c) {
                    const uint32_t at=cpuAddress+(y*8+x)*4+c;
#ifdef RECOMP_TRACK_MEMORY_WRITES
                    do_sb(state.RDRAM,0,at,color[c]);
#else
                    state.RDRAM[at^3]=color[c];
#endif
                }
            }
        };
        writeCPUImage(false);
        for(unsigned frame=0;;++frame) {
            if(frame<600) {
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
                if(frame>=120 && mode==1) continue;
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
            if(frame>=120) {
              for(unsigned panel=0;panel<(frame>=360?2U:1U);++panel) {
                RT64::FastDraw test;
                test.colorAddress=0x100000; test.depthAddress=0x200000;
                const auto &image=frame>=480?(panel?aliasAfter->texture:aliasBefore->texture):frame>=360?(panel?depthImageB->texture:depthImageA->texture):frame>=240?changedFeedback:feedback;
                test.otherMode={0,G_CYC_COPY}; test.textures[0]=image;
                test.tiles[0].cms=test.tiles[0].cmt=G_TX_CLAMP;
                test.tiles[0].lrs=(image->width-1)*4; test.tiles[0].lrt=(image->height-1)*4; test.vertices.resize(6);
                for(unsigned i=0;i<6;++i) {
                    const auto &p=quad[corners[i]]; auto &v=test.vertices[i];
                    v.position[0]=0.05f+(frame>=480?(panel+p[0])*0.425f:p[0]*0.85f);
                    v.position[1]=-0.9f+(frame>=360 && frame<480?((1-panel)+p[1])*0.425f:p[1]*0.85f);
                    v.uv[0]=p[0]*image->width; v.uv[1]=(1-p[1])*image->height;
                }
                sink->draw(test);
              }
                if(frame==120) trace("Framebuffer feedback panel: expected red above blue, no green");
                if(frame==240) trace("Same-value CPU store panel: expected red above blue (0,24,255), no cyan");
                if(frame==360) trace("Shared depth panel: expected red/blue above magenta/yellow");
                if(frame==480) trace("Alias panel: left original red/blue; right keeps red top and blue lower-left, green on right from row three");
            }
            sink->fullSync();
            // Repeated captures distinguish a first-read synchronization failure
            // from an unreadable surface. These frames use the same draw list.
            if(frame==2 || frame==3 || frame==4 || frame==30) capture(frame);
            if(frame>=60 && frame<64) checkGPUReadback(*sink,frame-60);
            sink->present(0x100000);
            if(frame==0) trace("First GBI fill and RGB triangle presented");
            if(frame==119) trace("120 frames presented successfully");
            } else {
                if(frame==660) writeCPUImage(true);
#ifdef RECOMP_TRACK_MEMORY_WRITES
                submit_framebuffer_writes(*sink);
#endif
                sink->present(cpuVI);
                if(frame==600) trace("CPU-only VI scanout: expected yellow above cyan, with no RDP draw");
                if(frame==660) trace("CPU-only VI update: expected yellow above magenta, with no RDP draw");
                if(frame==601 || frame==661) {
                    std::vector<uint8_t> bytes;
                    if(!sink->readFramebuffer(cpuAddress,256,bytes) || bytes.size()!=256)
                        throw std::runtime_error("CPU-only scanout readback failed");
                    for(unsigned i=0;i<bytes.size();++i) if(bytes[i]!=state.RDRAM[(cpuAddress+i)^3])
                        throw std::runtime_error("CPU-only scanout readback changed RAM bytes");
                    trace("CPU-only scanout byte readback matched guest RAM");
                }
            }
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
