#include "fast/rt64_fast.h"
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

int _newlib_heap_size_user=96*1024*1024;
unsigned int sceUserMainThreadStackSize=2*1024*1024;
unsigned int _pthread_stack_default_user=2*1024*1024;
extern "C" SceGxmContext *gxm_context;
namespace {
const SceGxmVertexProgram *lastVertex=nullptr;
const SceGxmFragmentProgram *lastFragment=nullptr;
void *lastVertexUniform=nullptr,*lastFragmentUniform=nullptr;
}
extern "C" void __real_sceGxmSetVertexProgram(SceGxmContext *,const SceGxmVertexProgram *);
extern "C" void __wrap_sceGxmSetVertexProgram(SceGxmContext *c,const SceGxmVertexProgram *p) { lastVertex=p;__real_sceGxmSetVertexProgram(c,p); }
extern "C" void __real_sceGxmSetFragmentProgram(SceGxmContext *,const SceGxmFragmentProgram *);
extern "C" void __wrap_sceGxmSetFragmentProgram(SceGxmContext *c,const SceGxmFragmentProgram *p) { lastFragment=p;__real_sceGxmSetFragmentProgram(c,p); }
extern "C" int __real_sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *,void **);
extern "C" int __wrap_sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *c,void **p) { int r=__real_sceGxmReserveVertexDefaultUniformBuffer(c,p);if(!r)lastVertexUniform=*p;return r; }
extern "C" int __real_sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *,void **);
extern "C" int __wrap_sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *c,void **p) { int r=__real_sceGxmReserveFragmentDefaultUniformBuffer(c,p);if(!r)lastFragmentUniform=*p;return r; }
extern "C" int __real_sceGxmSetVertexDefaultUniformBuffer(SceGxmContext *,void *);
extern "C" int __wrap_sceGxmSetVertexDefaultUniformBuffer(SceGxmContext *c,void *p) { lastVertexUniform=p;return __real_sceGxmSetVertexDefaultUniformBuffer(c,p); }
extern "C" int __real_sceGxmSetFragmentDefaultUniformBuffer(SceGxmContext *,void *);
extern "C" int __wrap_sceGxmSetFragmentDefaultUniformBuffer(SceGxmContext *c,void *p) { lastFragmentUniform=p;return __real_sceGxmSetFragmentDefaultUniformBuffer(c,p); }
namespace {
void check(bool ok,const char *why) { if(!ok)throw std::runtime_error(why); }
void api(int code,const char *name) { if(code<0)throw std::runtime_error(std::string(name)+" error "+std::to_string(code)); }
uint64_t now() { return sceKernelGetProcessTimeWide(); }
struct Memory {
    std::vector<void *> blocks;
    void *readback=nullptr;
    void *make(size_t size) { void *p=vglMemalign(64,std::max<size_t>(64,size));if(!p)throw std::bad_alloc();blocks.push_back(p);std::memset(p,0,std::max<size_t>(64,size));return p; }
    ~Memory() { glFinish();for(void *p:blocks)vglFree(p); }
};
GLuint shader(GLenum type,const char *text) {
    GLuint s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);
    GLint success=0;glGetShaderiv(s,GL_COMPILE_STATUS,&success);
    if(!success) { char log[4096];glGetShaderInfoLog(s,sizeof(log),nullptr,log);throw std::runtime_error(log); }
    return s;
}
struct Packet {
    SceGxmPrecomputedVertexState vertex{};
    SceGxmPrecomputedFragmentState fragment{};
    SceGxmPrecomputedDraw draw{};
    void *vuniform=nullptr,*funiform=nullptr;
    std::array<std::vector<uint8_t>,2> vdata,fdata;
};
std::vector<uint8_t> readImage(Memory &mem) {
    glFinish();if(!mem.readback)mem.readback=mem.make(320*240*4);void *p=mem.readback;
    vglReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,p);
    check(glGetError()==GL_NO_ERROR,"Native packet color read failed");
    return {static_cast<uint8_t *>(p),static_cast<uint8_t *>(p)+320*240*4};
}
}
int main() {
    sceIoMkdir("ux0:data/rt64-architecture",0777);
    FILE *log=std::fopen("ux0:data/rt64-architecture/results.log","w");if(!log)return 1;
    std::setvbuf(log,nullptr,_IONBF,0);
    try {
        auto context=RT64::createFastVitaGLSink(false,false);
        Memory memory;
        const char *vs=R"(#version 100
attribute vec4 aPosition;
attribute vec2 aUV;
attribute vec4 aColor;
attribute float aFog;
uniform mat4 uMatrix;
varying vec2 vUV;
varying vec4 vColor;
varying float vFog;
void main(){gl_Position=uMatrix*aPosition;vUV=aUV;vColor=aColor;vFog=aFog;}
)";
        const char *fs=R"(#version 100
precision highp float;
uniform sampler2D uImage;
uniform vec4 uTint;
varying vec2 vUV;
varying vec4 vColor;
varying float vFog;
void main(){gl_FragColor=mix(texture2D(uImage,vUV)*vColor*uTint,vec4(0.2,0.3,0.4,1.0),vFog*0.1);}
)";
        GLuint vshader=shader(GL_VERTEX_SHADER,vs),fshader=shader(GL_FRAGMENT_SHADER,fs),program=glCreateProgram();
        glAttachShader(program,vshader);glAttachShader(program,fshader);
        const char *names[]={"aPosition","aUV","aColor","aFog"};
        for(unsigned i=0;i<4;++i)glBindAttribLocation(program,i,names[i]);
        glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);check(linked,"Native packet program link failed");
        glUseProgram(program);
        GLint matrixLocation=glGetUniformLocation(program,"uMatrix"),tintLocation=glGetUniformLocation(program,"uTint");
        glUniform1i(glGetUniformLocation(program,"uImage"),0);
        GLuint texture=0,color=0,fbo=0,vbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        uint8_t texels[64];for(unsigned i=0;i<64;++i)texels[i]=i%4==3?255:uint8_t(64+i*3);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,texels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        const SceGxmTexture nativeTexture=*vglGetGxmTexture(GL_TEXTURE_2D);
        glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,320,240,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Native packet framebuffer failed");
        glViewport(0,0,320,240);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
        constexpr unsigned perDraw=66,drawCount=170;
        auto *vertex=static_cast<RT64::FastVertex *>(memory.make(perDraw*sizeof(RT64::FastVertex)));
        auto *indices=static_cast<uint16_t *>(memory.make(perDraw*sizeof(uint16_t)));
        for(unsigned i=0;i<perDraw;++i) {
            vertex[i]=RT64::FastVertex{};indices[i]=uint16_t(i);
            const float xy[3][2]={{-0.035f,-0.035f},{0.035f,-0.035f},{0.0f,0.035f}};
            vertex[i].position[0]=xy[i%3][0]+float(i/3)*0.002f;vertex[i].position[1]=xy[i%3][1];
            vertex[i].uv[0]=float(i%3)/2;vertex[i].uv[1]=float(i/3)/22;
            vertex[i].color[0]=float((i*13)%127+128)/255;vertex[i].color[1]=0.8f;vertex[i].color[2]=0.6f;vertex[i].fog=0.25f;
        }
        glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,perDraw*sizeof(RT64::FastVertex),vertex,GL_STATIC_DRAW);
        const unsigned offsets[]={0,16,24,40};const GLint sizes[]={4,2,4,1};
        for(unsigned i=0;i<4;++i) { glEnableVertexAttribArray(i);glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,sizeof(RT64::FastVertex),reinterpret_cast<void *>(offsets[i])); }
        auto setUniforms=[&](unsigned draw,unsigned phase) {
            float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,-0.9f+float(draw%17)*0.11f,-0.8f+float(draw/17)*0.17f,0,1};
            m[0]=1.0f+phase*0.1f;m[5]=1.0f-phase*0.1f;
            const float tint[]={1.0f,float(12+draw%5)/16.0f,phase?0.7f:0.9f,1.0f};
            glUniformMatrix4fv(matrixLocation,1,GL_FALSE,m);glUniform4fv(tintLocation,1,tint);
        };
        std::vector<Packet> packets(drawCount);
        const SceGxmVertexProgram *vp=nullptr;const SceGxmFragmentProgram *fp=nullptr;
        size_t vertexUniformBytes=0,fragmentUniformBytes=0;
        for(unsigned phase=0;phase<2;++phase)for(unsigned i=0;i<drawCount;++i) {
            setUniforms(i,phase);glDrawArrays(GL_TRIANGLES,0,perDraw);
            check(lastVertex && lastFragment && lastVertexUniform && lastFragmentUniform,"Native packet capture missed state");
            if(!vp) { vp=lastVertex;fp=lastFragment;
                vertexUniformBytes=sceGxmProgramGetDefaultUniformBufferSize(sceGxmVertexProgramGetProgram(vp));
                fragmentUniformBytes=sceGxmProgramGetDefaultUniformBufferSize(sceGxmFragmentProgramGetProgram(fp));
                check(vertexUniformBytes && fragmentUniformBytes && vertexUniformBytes<=4096 && fragmentUniformBytes<=4096,"Invalid uniform size"); }
            check(vp==lastVertex && fp==lastFragment,"Native packet pipeline changed");
            auto &p=packets[i];p.vdata[phase].assign(static_cast<uint8_t *>(lastVertexUniform),static_cast<uint8_t *>(lastVertexUniform)+vertexUniformBytes);
            p.fdata[phase].assign(static_cast<uint8_t *>(lastFragmentUniform),static_cast<uint8_t *>(lastFragmentUniform)+fragmentUniformBytes);
        }
        glFinish();
        for(auto &p:packets) {
            p.vuniform=memory.make(vertexUniformBytes);p.funiform=memory.make(fragmentUniformBytes);
            api(sceGxmPrecomputedVertexStateInit(&p.vertex,vp,memory.make(sceGxmGetPrecomputedVertexStateSize(vp))),"vertex precompute");
            api(sceGxmPrecomputedFragmentStateInit(&p.fragment,fp,memory.make(sceGxmGetPrecomputedFragmentStateSize(fp))),"fragment precompute");
            sceGxmPrecomputedVertexStateSetDefaultUniformBuffer(&p.vertex,p.vuniform);
            sceGxmPrecomputedFragmentStateSetDefaultUniformBuffer(&p.fragment,p.funiform);
            api(sceGxmPrecomputedFragmentStateSetTexture(&p.fragment,0,&nativeTexture),"texture precompute");
            api(sceGxmPrecomputedDrawInit(&p.draw,vp,memory.make(sceGxmGetPrecomputedDrawSize(vp))),"draw precompute");
            const void *streams[16];std::fill(std::begin(streams),std::end(streams),vertex);
            api(sceGxmPrecomputedDrawSetAllVertexStreams(&p.draw,streams),"streams precompute");
            sceGxmPrecomputedDrawSetParams(&p.draw,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,perDraw);
        }
        std::fprintf(log,"clocks=%d,%d,%d,%d draws=%u vertices=%u uniform_bytes=%u,%u\n",scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuXbarClockFrequency(),drawCount,drawCount*perDraw,unsigned(vertexUniformBytes),unsigned(fragmentUniformBytes));
        std::array<std::vector<uint8_t>,2> expected;
        for(unsigned trial=0;trial<14;++trial)for(unsigned position=0;position<4;++position) {
            const unsigned mode=(position+trial)%4,phase=trial&1;
            sceGxmSetPrecomputedVertexState(gxm_context,nullptr);sceGxmSetPrecomputedFragmentState(gxm_context,nullptr);
            glUseProgram(program);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
            glBindBuffer(GL_ARRAY_BUFFER,vbo);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            setUniforms(0,phase);glDrawArrays(GL_TRIANGLES,0,perDraw);glFinish();
            const auto started=now();
            if(mode>=2)for(auto &p:packets) { std::memcpy(p.vuniform,p.vdata[phase].data(),vertexUniformBytes);std::memcpy(p.funiform,p.fdata[phase].data(),fragmentUniformBytes); }
            const auto prepared=now();
            for(unsigned i=0;i<drawCount;++i) {
                auto &p=packets[i];
                if(mode<2) {
                    setUniforms(i,phase);glBindBuffer(GL_ARRAY_BUFFER,vbo);
                    if(mode==0)glBufferData(GL_ARRAY_BUFFER,perDraw*sizeof(RT64::FastVertex),vertex,GL_STREAM_DRAW);
                    glDrawArrays(GL_TRIANGLES,0,perDraw);
                } else if(mode==2) {
                    sceGxmSetVertexProgram(gxm_context,vp);sceGxmSetFragmentProgram(gxm_context,fp);
                    api(sceGxmSetVertexDefaultUniformBuffer(gxm_context,p.vuniform),"vertex uniforms");
                    api(sceGxmSetFragmentDefaultUniformBuffer(gxm_context,p.funiform),"fragment uniforms");
                    for(unsigned stream=0;stream<4;++stream)api(sceGxmSetVertexStream(gxm_context,stream,vertex),"vertex stream");
                    api(sceGxmSetFragmentTexture(gxm_context,0,&nativeTexture),"fragment texture");
                    api(sceGxmDraw(gxm_context,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,perDraw),"native draw");
                } else {
                    sceGxmSetPrecomputedVertexState(gxm_context,&p.vertex);sceGxmSetPrecomputedFragmentState(gxm_context,&p.fragment);
                    api(sceGxmDrawPrecomputed(gxm_context,&p.draw),"precomputed draw");
                }
            }
            sceGxmSetPrecomputedVertexState(gxm_context,nullptr);sceGxmSetPrecomputedFragmentState(gxm_context,nullptr);
            const auto submitted=now();glFinish();const auto complete=now();
            const auto image=readImage(memory);
            if(expected[phase].empty()) {
                std::set<uint32_t> colors;
                for(size_t at=0;at<image.size();at+=4) { uint32_t value;std::memcpy(&value,image.data()+at,4);colors.insert(value); }
                check(colors.size()>16,"Native packet image lacks visual content");
                std::fprintf(log,"witness phase=%u unique_colors=%u\n",phase,unsigned(colors.size()));
                char path[128];std::snprintf(path,sizeof(path),"ux0:data/rt64-architecture/phase-%u.ppm",phase);
                FILE *ppm=std::fopen(path,"wb");check(ppm,"Native packet image export failed");
                std::fprintf(ppm,"P6\n320 240\n255\n");
                for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)std::fwrite(image.data()+((239-y)*320+x)*4,1,3,ppm);
                std::fclose(ppm);expected[phase]=image;
                if(!expected[0].empty() && !expected[1].empty())check(expected[0]!=expected[1],"Native uniform phases did not change the image");
            } else check(expected[phase]==image,"Native packet image differs from GL control");
            check(glGetError()==GL_NO_ERROR,"Native packet GL error");
            std::fprintf(log,"sample trial=%u phase=%u mode=%u prepare_us=%llu submit_us=%llu complete_us=%llu exact=1\n",trial,phase,mode,(unsigned long long)(prepared-started),(unsigned long long)(submitted-started),(unsigned long long)(complete-started));
        }
        glFinish();glDeleteBuffers(1,&vbo);glDeleteProgram(program);glDeleteShader(vshader);glDeleteShader(fshader);
        glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&color);glDeleteTextures(1,&texture);
        std::fprintf(log,"PASS: GXM packet architecture control\n");std::fclose(log);return 0;
    } catch(const std::exception &error) { std::fprintf(log,"FAIL: %s\n",error.what());std::fclose(log);return 1; }
}
