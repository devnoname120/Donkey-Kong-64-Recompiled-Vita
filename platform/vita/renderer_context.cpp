#include "fast/rt64_fast_interpreter.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/ultramodern.hpp"
#include <cstdio>
#include <psp2/kernel/processmgr.h>
#include "log.h"
#include "draw_trace.h"
#include "vi.h"
#include "memory_writes.h"
#if DK64_VITA_PROFILE_FUNCTIONS || DK64_VITA_TRACE_RENDERER
#include <unordered_set>
#endif
#if DK64_VITA_PROFILE_FUNCTIONS || DK64_VITA_TRACE_RENDERER || DK64_VITA_SCRIPTED_INPUT
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
                // GPU views have no CPU pixel payload to dump.
                if(t.storage) continue;
                char path[128]; std::snprintf(path,sizeof(path),"ux0:data/dk64recompiled/texture-%016llx.rgba",static_cast<unsigned long long>(t.hash));
                SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
                if(fd>=0) { const int written=sceIoWrite(fd,t.rgba.data(),t.rgba.size()); sceIoClose(fd); if(written!=int(t.rgba.size())) vita_log("Texture capture write failed"); }
            }
        }
        backend->draw(draw);
    }
    void fullSync() override { backend->fullSync(); }
    void flushDraws() override { backend->flushDraws(); }
    void setRDRAM(const uint8_t *rdram,size_t size) override { backend->setRDRAM(rdram,size); }
    void setMemoryWriteTracking(std::function<void(uint32_t,uint32_t,bool)> watch) override { backend->setMemoryWriteTracking(std::move(watch)); }
    void notifyMemoryWrites(const std::vector<RT64::FastMemoryWrite> &writes) override { backend->notifyMemoryWrites(writes); }
    std::shared_ptr<const RT64::FastFramebuffer> snapshotFramebuffer(uint32_t address,uint32_t size) override {
        return backend->snapshotFramebuffer(address,size);
    }
    bool readFramebufferSnapshot(const RT64::FastFramebuffer &snapshot,std::vector<uint8_t> &bytes) override {
        return backend->readFramebufferSnapshot(snapshot,bytes);
    }
    void present(uint32_t address) override { backend->present(address); }
    void present(const RT64::VI &vi) override { backend->present(vi); }
    bool readFramebuffer(uint32_t address,uint32_t size,std::vector<uint8_t> &bytes) override {
        return backend->readFramebuffer(address,size,bytes);
    }
};
#endif
class VitaRendererContext final : public ultramodern::renderer::RendererContext {
    std::unique_ptr<RT64::FastDrawSink> sink;
    std::unique_ptr<RT64::State> state;
    RT64::Interpreter interpreter;
#if DK64_VITA_DIAGNOSTICS
    uint64_t tasks = 0;
    uint64_t task_time_us = 0;
    unsigned vi_updates = 0;
#endif
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
            track_framebuffer_writes(*sink);
            interpreter.setup(state.get());
            setup_result = ultramodern::renderer::SetupResult::Success;
            vita_log("DK64 Vita renderer initialized");
        } catch(const std::exception &e) { vita_log("Vita renderer initialization: %s",e.what()); }
    }
    bool valid() override { return setup_result == ultramodern::renderer::SetupResult::Success; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&,const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}
    bool defer_rsp_completion() const override { return true; }
    void send_dl(const OSTask *task) override {
        submit_framebuffer_writes(*sink);
#if DK64_VITA_DIAGNOSTICS
        if(tasks<3) vita_log("Entering graphics task %llu",static_cast<unsigned long long>(tasks+1));
#endif
        try {
#if DK64_VITA_PROFILE_FUNCTIONS || DK64_VITA_TRACE_RENDERER
            static_cast<TraceSink *>(sink.get())->startTask(tasks+1);
#endif
#if DK64_VITA_DIAGNOSTICS
            const uint64_t started=sceKernelGetProcessTimeWide();
#endif
            interpreter.loadUCodeGBI(task->t.ucode,task->t.ucode_data,true);
            uint32_t address = uint32_t(task->t.data_ptr) & 0xffffff;
            interpreter.processDisplayLists(address,reinterpret_cast<RT64::DisplayList *>(state->fromRDRAM(address)));
#if DK64_VITA_DIAGNOSTICS
            ++tasks;
            task_time_us+=sceKernelGetProcessTimeWide()-started;
            if(tasks==1 || tasks%120==0) vita_log("DK64 graphics tasks completed: %llu, average processing %.2f ms",
                static_cast<unsigned long long>(tasks),task_time_us/(tasks==1?1000.0:120000.0));
            if(tasks%120==0) task_time_us=0;
#endif
        } catch(const std::exception &e) {
            vita_log("DK64 graphics task %llu: %s",static_cast<unsigned long long>(tasks),e.what());
            ultramodern::quit();
        }
    }
    void send_dummy_workload(uint32_t address) override { submit_framebuffer_writes(*sink); sink->present(address & 0xffffff); }
    std::vector<uint8_t> read_framebuffer(uint32_t address,uint32_t size) override {
        submit_framebuffer_writes(*sink);
        std::vector<uint8_t> bytes;
        sink->readFramebuffer(address,size,bytes);
#if DK64_VITA_SCRIPTED_INPUT
        // Preserve the actual game-requested bytes without issuing an extra
        // read that could change the emulator's first-read behavior.
        static unsigned readbacks=0;
        if(readbacks<8) {
            char path[128];
            std::snprintf(path,sizeof(path),"ux0:data/dk64recompiled-probe/readback-%u-%08x.bin",readbacks,address);
            SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
            const int written=fd>=0?sceIoWrite(fd,bytes.data(),bytes.size()):-1;
            if(fd>=0) sceIoClose(fd);
            vita_log("Game framebuffer readback %u: address=%08x requested=%u returned=%u written=%d graphics_tasks=%llu",
                readbacks,address,size,unsigned(bytes.size()),written,static_cast<unsigned long long>(tasks));
            ++readbacks;
        }
#endif
        return bytes;
    }
    void update_screen() override {
        submit_framebuffer_writes(*sink);
        const auto *vi = ultramodern::renderer::get_vi_regs();
#if DK64_VITA_DIAGNOSTICS
        if(tasks && vi_updates++ < 5) vita_log("VI origin 0x%08x, color image 0x%08x",vi->VI_ORIGIN_REG,state->rdp->parameters.colorAddress);
#endif
        sink->present(vita_video_interface(*vi));
#if DK64_VITA_DIAGNOSTICS
        if(tasks && (vi_updates<=5 || vi_updates%60==0)) vita_log("VI presentation returned, update %u",vi_updates);
#endif
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
