#include "fast/rt64_fast_interpreter.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/ultramodern.hpp"
#include <cstdio>
#include <psp2/kernel/processmgr.h>
#include "log.h"
#include "draw_trace.h"
#include "vi.h"
#if DK64_VITA_PROFILE_FUNCTIONS || DK64_VITA_TRACE_RENDERER
#include <unordered_set>
#include <psp2/io/fcntl.h>
#endif

namespace {
#if DK64_VITA_PROFILE_FUNCTIONS || DK64_VITA_TRACE_RENDERER
class TraceSink final : public RT64::FastDrawSink {
    std::unique_ptr<RT64::FastDrawSink> backend;
    std::unordered_set<uint64_t> textures;
    unsigned draw_reports=0;
public:
    explicit TraceSink(std::unique_ptr<RT64::FastDrawSink> sink) : backend(std::move(sink)) {}
    void startTask(uint64_t task) {
        if(task==480) { draw_reports=0; textures.clear(); vita_log("Capturing render task 480"); }
    }
    void draw(const RT64::FastDraw &draw) override {
        if(draw_reports<14) trace_fast_draw(draw,++draw_reports,vita_log);
        for(unsigned i=0;i<2;++i) if(draw.textures[i] && textures.size()<8) {
            const auto &t=*draw.textures[i];
            if(textures.insert(t.hash).second) {
                vita_log("Decoded texture %016llx %ux%u fmt=%u siz=%u",static_cast<unsigned long long>(t.hash),t.width,t.height,draw.tiles[i].fmt,draw.tiles[i].siz);
                char path[128]; std::snprintf(path,sizeof(path),"ux0:data/dk64recompiled/texture-%016llx.rgba",static_cast<unsigned long long>(t.hash));
                SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
                if(fd>=0) { const int written=sceIoWrite(fd,t.rgba.data(),t.rgba.size()); sceIoClose(fd); if(written!=int(t.rgba.size())) vita_log("Texture capture write failed"); }
            }
        }
        backend->draw(draw);
    }
    void fullSync() override { backend->fullSync(); }
    void present(uint32_t address) override { backend->present(address); }
    void present(const RT64::VI &vi) override { backend->present(vi); }
};
#endif
class VitaRendererContext final : public ultramodern::renderer::RendererContext {
    std::unique_ptr<RT64::FastDrawSink> sink;
    std::unique_ptr<RT64::State> state;
    RT64::Interpreter interpreter;
    uint64_t tasks = 0;
    uint64_t task_time_us = 0;
    unsigned vi_updates = 0;
public:
    explicit VitaRendererContext(uint8_t *rdram) {
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
        setup_result = ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
        try {
            // N64ModernRuntime's VI thread already provides frame pacing.
            sink = RT64::createFastVitaGLSink(false);
#if DK64_VITA_PROFILE_FUNCTIONS || DK64_VITA_TRACE_RENDERER
            sink=std::make_unique<TraceSink>(std::move(sink));
#endif
            state = std::make_unique<RT64::State>(rdram,recomp::mem_size,*sink);
            interpreter.setup(state.get());
            setup_result = ultramodern::renderer::SetupResult::Success;
            vita_log("DK64 Vita renderer initialized");
        } catch(const std::exception &e) { vita_log("Vita renderer initialization: %s",e.what()); }
    }
    bool valid() override { return setup_result == ultramodern::renderer::SetupResult::Success; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&,const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}
    void send_dl(const OSTask *task) override {
        if(tasks<3) vita_log("Entering graphics task %llu",static_cast<unsigned long long>(tasks+1));
        try {
#if DK64_VITA_PROFILE_FUNCTIONS || DK64_VITA_TRACE_RENDERER
            static_cast<TraceSink *>(sink.get())->startTask(tasks+1);
#endif
            const uint64_t started=sceKernelGetProcessTimeWide();
            interpreter.loadUCodeGBI(task->t.ucode,task->t.ucode_data,true);
            uint32_t address = uint32_t(task->t.data_ptr) & 0xffffff;
            interpreter.processDisplayLists(address,reinterpret_cast<RT64::DisplayList *>(state->fromRDRAM(address)));
            ++tasks;
            task_time_us+=sceKernelGetProcessTimeWide()-started;
            if(tasks==1 || tasks%120==0) vita_log("DK64 graphics tasks completed: %llu, average processing %.2f ms",
                static_cast<unsigned long long>(tasks),task_time_us/(tasks==1?1000.0:120000.0));
            if(tasks%120==0) task_time_us=0;
        } catch(const std::exception &e) {
            vita_log("DK64 graphics task %llu: %s",static_cast<unsigned long long>(tasks),e.what());
            ultramodern::quit();
        }
    }
    void send_dummy_workload(uint32_t address) override { sink->present(address & 0xffffff); }
    void update_screen() override {
        const auto *vi = ultramodern::renderer::get_vi_regs();
        if(tasks && vi_updates++ < 5) vita_log("VI origin 0x%08x, color image 0x%08x",vi->VI_ORIGIN_REG,state->rdp->parameters.colorAddress);
        sink->present(vita_video_interface(*vi));
        if(tasks && (vi_updates<=5 || vi_updates%60==0)) vita_log("VI presentation returned, update %u",vi_updates);
    }
    void shutdown() override { state.reset(); sink.reset(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1; }
};
}

std::unique_ptr<ultramodern::renderer::RendererContext> create_vita_renderer(
    uint8_t *rdram,ultramodern::renderer::WindowHandle,bool) {
    return std::make_unique<VitaRendererContext>(rdram);
}
