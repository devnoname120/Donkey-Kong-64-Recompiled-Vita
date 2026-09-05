#include "egl.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdexcept>
#include <cstdio>
#include <fstream>
#include <filesystem>

extern unsigned probeCompletedTasks();
extern bool probeBatchingEnabled();
namespace {
class Context final : public ProbeEGL {
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    std::string directory;
    unsigned capture_index=0;
    void swap() {
        constexpr unsigned wanted[]={1,2,3,10,30,60,120,240,480,720,960,1200,1800};
        const unsigned task=probeCompletedTasks();
        if(capture_index<std::size(wanted) && task>=wanted[capture_index]) {
            std::vector<uint8_t> rgba(960*544*4),rgb(960*544*3);
            glReadPixels(0,0,960,544,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
            if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Host GL readback failed");
            for(unsigned y=0;y<544;++y) for(unsigned x=0;x<960;++x) for(unsigned c=0;c<3;++c)
                rgb[(y*960+x)*3+c]=rgba[((543-y)*960+x)*4+c];
            char name[40]; std::snprintf(name,sizeof(name),"frame-%06u.ppm",task);
            std::ofstream out(std::filesystem::path(directory)/name,std::ios::binary);
            out<<"P6\n960 544\n255\n"; out.write(reinterpret_cast<const char *>(rgb.data()),rgb.size());
            std::fprintf(stderr,"Host GL captured %s\n",name);
            ++capture_index;
        }
        if(!eglSwapBuffers(display,surface)) throw std::runtime_error("Host EGL swap failed");
    }
public:
    explicit Context(std::string directory) : directory(std::move(directory)) {
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if(display==EGL_NO_DISPLAY || !eglInitialize(display,nullptr,nullptr)) throw std::runtime_error("Cannot initialize host EGL");
        if(!eglBindAPI(EGL_OPENGL_ES_API)) throw std::runtime_error("Cannot select GLES API");
        const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,16,EGL_NONE};
        EGLConfig config{}; EGLint count=0;
        if(!eglChooseConfig(display,attrs,&config,1,&count)||!count) throw std::runtime_error("No host GLES framebuffer config");
        const EGLint size[]={EGL_WIDTH,960,EGL_HEIGHT,544,EGL_NONE};
        surface=eglCreatePbufferSurface(display,config,size);
        const EGLint version[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);
        if(surface==EGL_NO_SURFACE||context==EGL_NO_CONTEXT||!eglMakeCurrent(display,surface,surface,context))
            throw std::runtime_error("Cannot create host GLES context");
        std::fprintf(stderr,"Host GL renderer: %s\n",glGetString(GL_RENDERER));
    }
    ~Context() override {
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(context!=EGL_NO_CONTEXT) eglDestroyContext(display,context);
        if(surface!=EGL_NO_SURFACE) eglDestroySurface(display,surface);
        if(display!=EGL_NO_DISPLAY) eglTerminate(display);
    }
    std::unique_ptr<RT64::FastDrawSink> createSink() override {
        std::fprintf(stderr,"Host GL batching: %s\n",probeBatchingEnabled()?"enabled":"disabled");
        return RT64::createFastGLES2Sink([this] { swap(); },probeBatchingEnabled());
    }
};
}
std::unique_ptr<ProbeEGL> createProbeEGL(const std::string &directory) { return std::make_unique<Context>(directory); }
