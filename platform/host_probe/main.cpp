#include "fast/rt64_fast_interpreter.h"
#include "librecomp/game.hpp"
#include "librecomp/addresses.hpp"
#include "donk_game.h"
#include "../vita/draw_trace.h"
#include "../vita/vi.h"
#include "../vita/log.h"
#include "../vita/adventure_probe.h"
#include "ovl_patches.hpp"
#include <atomic>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_set>
#ifdef PROBE_GL
#include "egl.h"
#endif

extern "C" void recomp_entrypoint(uint8_t *,recomp_context *);
extern gpr get_entrypoint_address();
extern RspUcodeFunc n_aspMain;
static std::atomic<unsigned> completed_tasks{0};
static uint8_t *game_memory=nullptr;
static auto start=std::chrono::steady_clock::now();
static int run_seconds=45, present_ms=0;
static bool enable_batching=true;
static bool adventure_input=false;
static bool pause_input=false;
static std::string probe_directory;
unsigned probeCompletedTasks() { return completed_tasks.load(); }
bool probeBatchingEnabled() { return enable_batching; }

void vita_log(const char *format,...) {
    va_list args; va_start(args,format); std::vfprintf(stderr,format,args); va_end(args);
    std::fputc('\n',stderr); std::fflush(stderr);
}
class Capture final : public RT64::FastDrawSink {
public:
    uint64_t draws=0,triangles=0,textured=0;
    std::unique_ptr<RT64::FastDrawSink> forward;
    RT64::State *source=nullptr;
    std::unordered_set<uint64_t> captured_textures;
    unsigned draw_reports=0;
    void startTask(unsigned task) {
        if(task==480) { draw_reports=0; captured_textures.clear(); vita_log("Capturing render task 480"); }
    }
    void draw(const RT64::FastDraw &draw) override {
        ++draws; triangles+=draw.vertices.size()/3;
        textured+=bool(draw.textures[0]||draw.textures[1]);
        if(draw_reports<14) trace_fast_draw(draw,++draw_reports,vita_log);
        for(unsigned i=0;i<2;++i) if(draw.textures[i] && captured_textures.size()<8) {
            const auto &t=*draw.textures[i];
            if(captured_textures.insert(t.hash).second) vita_log("Decoded texture %016llx %ux%u fmt=%u siz=%u",
                static_cast<unsigned long long>(t.hash),t.width,t.height,draw.tiles[i].fmt,draw.tiles[i].siz);
        }
        if(draws<15) vita_log("Draw %llu image=%08x %ux%u bpp=%u rect=%d fill=%d depth=%d/%d fog=%d mux=%08x/%08x mode=%08x/%08x tex=%d/%d source=%08x",
            static_cast<unsigned long long>(draws),draw.colorAddress,draw.width,draw.height,draw.colorBytes,
            draw.rectangle,draw.fill,draw.depthTest,draw.depthWrite,draw.fog,draw.combine.L,draw.combine.H,draw.otherMode.H,draw.otherMode.L,
            bool(draw.textures[0]),bool(draw.textures[1]),source?source->rdp->textureAddress:0);
        if(draws<15 && source && !draw.rectangle && !draw.vertices.empty()) {
            const auto &v=draw.vertices[0];
            vita_log("  viewport=(%.1f,%.1f)+(%.1f,%.1f), clip=(%.3f,%.3f,%.3f,%.3f), shade=(%.3f,%.3f,%.3f,%.3f)",
                source->rsp->viewportTranslate[0],source->rsp->viewportTranslate[1],source->rsp->viewportScale[0],source->rsp->viewportScale[1],
                v.position[0],v.position[1],v.position[2],v.position[3],v.color[0],v.color[1],v.color[2],v.color[3]);
        }
        if(forward) forward->draw(draw);
    }
    void fullSync() override { if(forward) forward->fullSync(); }
    void present(uint32_t address) override {
        if(forward) forward->present(address);
        if(present_ms) std::this_thread::sleep_for(std::chrono::milliseconds(present_ms));
    }
    void present(const RT64::VI &vi) override {
        if(forward) forward->present(vi);
        if(present_ms) std::this_thread::sleep_for(std::chrono::milliseconds(present_ms));
    }
};
class Renderer final : public ultramodern::renderer::RendererContext {
#ifdef PROBE_GL
    std::unique_ptr<ProbeEGL> platform;
#endif
    Capture sink;
    RT64::State state;
    RT64::Interpreter interpreter;
public:
    explicit Renderer(uint8_t *rdram) : state(rdram,recomp::mem_size,sink) {
        sink.source=&state;
#ifdef PROBE_GL
        platform=createProbeEGL(probe_directory);
        sink.forward=platform->createSink();
#endif
        interpreter.setup(&state);
        setup_result=ultramodern::renderer::SetupResult::Success;
        chosen_api=ultramodern::renderer::GraphicsApi::Auto;
    }
    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&,const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}
    void send_dl(const OSTask *task) override {
        sink.startTask(completed_tasks.load()+1);
        interpreter.loadUCodeGBI(task->t.ucode,task->t.ucode_data,true);
        const uint32_t address=uint32_t(task->t.data_ptr)&0xffffff;
        interpreter.processDisplayLists(address,reinterpret_cast<RT64::DisplayList *>(state.fromRDRAM(address)));
        unsigned count=++completed_tasks;
        if(count<5) vita_log("Viewport scale=(%.2f,%.2f,%.3f), translation=(%.2f,%.2f,%.3f)",
            state.rsp->viewportScale[0],state.rsp->viewportScale[1],state.rsp->viewportScale[2],
            state.rsp->viewportTranslate[0],state.rsp->viewportTranslate[1],state.rsp->viewportTranslate[2]);
        if(count<5 || count%120==0) vita_log("Probe tasks=%u draws=%llu triangles=%llu textured=%llu",count,
            static_cast<unsigned long long>(sink.draws),static_cast<unsigned long long>(sink.triangles),static_cast<unsigned long long>(sink.textured));
    }
    void send_dummy_workload(uint32_t address) override { sink.present(address); }
    void update_screen() override {
        const auto *vi=ultramodern::renderer::get_vi_regs();
        static uint32_t last_width=0,last_h=0,last_control=0;
        if(completed_tasks.load() && (last_width!=vi->VI_WIDTH_REG || last_h!=vi->VI_H_START_REG || last_control!=vi->VI_STATUS_REG)) vita_log("VI task=%u width=%u control=%08x h=%08x v=%08x xscale=%08x yscale=%08x origin=%08x",completed_tasks.load(),
            vi->VI_WIDTH_REG,vi->VI_STATUS_REG,vi->VI_H_START_REG,vi->VI_V_START_REG,vi->VI_X_SCALE_REG,vi->VI_Y_SCALE_REG,vi->VI_ORIGIN_REG);
        last_width=vi->VI_WIDTH_REG; last_h=vi->VI_H_START_REG; last_control=vi->VI_STATUS_REG;
        sink.present(vita_video_interface(*vi));
    }
    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1; }
};

static void snapshot() {
    if(!game_memory) return;
    auto *rdram=game_memory;
    const auto *idle=TO_PTR(OSThread,int32_t(0x80012710));
    vita_log("Idle record: id=%d priority=%d queue=%08x next=%08x context=%p; running queue head=%08x",idle->id,idle->priority,
        uint32_t(idle->queue),uint32_t(idle->next),static_cast<void *>(idle->context),uint32_t(ultramodern::thread_queue_peek(rdram,ultramodern::running_queue)));
}
int main(int argc,char **argv) {
    if(argc<2) { std::fprintf(stderr,"Usage: %s ROM_DIRECTORY [seconds] [presentation_ms] [batching:0|1] [adventure|pause]\n",argv[0]); return 1; }
    if(argc>2) run_seconds=std::atoi(argv[2]);
    if(argc>3) present_ms=std::atoi(argv[3]);
    if(argc>4) enable_batching=std::atoi(argv[4])!=0;
    if(argc>5) { pause_input=std::string(argv[5])=="pause"; adventure_input=pause_input||std::string(argv[5])=="adventure"; }
    probe_directory=argv[1];
    std::setvbuf(stdout,nullptr,_IONBF,0); std::setvbuf(stderr,nullptr,_IONBF,0);
    std::set_terminate([] {
        try { if(auto error=std::current_exception()) std::rethrow_exception(error); }
        catch(const std::exception &e) { vita_log("PROBE EXCEPTION: %s",e.what()); }
        catch(...) { vita_log("PROBE unknown exception"); }
        snapshot(); std::_Exit(2);
    });
    recomp::register_config_path(argv[1]); dk64::register_bk_overlays();
    recomp::GameEntry game{};
    game.rom_hash=0x4d876060f09b3fc5ULL; game.internal_name="DONKEY KONG 64";
    game.display_name="Donkey Kong 64"; game.game_id=u8"DK64"; game.mod_game_id="dk64";
    game.save_type=recomp::SaveType::Eep16k; game.is_enabled=true;
    game.entrypoint_address=get_entrypoint_address(); game.entrypoint=&recomp_entrypoint;
    game.on_init_callback=[](uint8_t *rdram,recomp_context *ctx) { game_memory=rdram; dk64::dk_on_init(rdram,ctx); };
    recomp::register_game(game);
    char name[]="probe",flag[]="--game",id[]="dk64"; char *args[]={name,flag,id};
    recomp::Configuration config{};
    config.argc=3; config.argv=args; config.project_version={1,0,2};
    config.gfx_callbacks.create_window=[](void *) -> SDL_Window * { return nullptr; };
    config.renderer_callbacks.create_render_context=[](uint8_t *ram,ultramodern::renderer::WindowHandle,bool) -> std::unique_ptr<ultramodern::renderer::RendererContext> { return std::make_unique<Renderer>(ram); };
    config.rsp_callbacks.get_rsp_microcode=[](const OSTask *task) -> RspUcodeFunc * { return task->t.type==M_AUDTASK?&n_aspMain:nullptr; };
    config.input_callbacks.get_input=[](int controller,uint16_t *buttons,float *x,float *y) {
        *buttons=0; *x=0; *y=0;
        if(controller!=0) return false;
        if(adventure_input) {
            static AdventureProbe probe;
            probe.poll(game_memory,buttons,x,y,pause_input);
        }
        return true;
    };
    config.input_callbacks.get_connected_device_info=[](int controller) { using namespace ultramodern::input;return connected_device_info_t{controller==0?Device::Controller:Device::None,Pak::None}; };
    config.audio_callbacks.queue_samples=[](int16_t *,size_t) {};
    config.audio_callbacks.get_frames_remaining=[]() -> size_t { return 0; };
    config.audio_callbacks.set_frequency=[](uint32_t frequency) { vita_log("Probe audio rate=%u",frequency); };
    config.error_handling_callbacks.message_box=[](const char *message) { vita_log("PROBE ERROR: %s",message); };
    config.gfx_callbacks.update_gfx=[](void *) {
        if(std::chrono::steady_clock::now()-start>=std::chrono::seconds(run_seconds)) {
            vita_log("PROBE elapsed=%d completed_tasks=%u",run_seconds,completed_tasks.load());
            // This bounded diagnostic terminates its whole process. It does not
            // claim to exercise the game's normal shutdown sequence.
            std::_Exit(completed_tasks.load()>=120?0:3);
        }
    };
    config.message_queue_control.requeue_timer=false;
    recomp::start(config);
}
