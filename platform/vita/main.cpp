#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <cstdio>
#include <mutex>
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <atomic>
#include "log.h"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "donk_game.h"
#include "ovl_patches.hpp"
#include "audio_queue.h"
#if DK64_VITA_AUDIO_CAPTURE
#include "audio_capture.h"
#include <psp2/audioout.h>
static constexpr char data_directory[]="ux0:data/dk64recompiled-audio";
#elif DK64_VITA_SCRIPTED_INPUT
#include "adventure_probe.h"
#if DK64_VITA_MAP_PROBE_ENABLED
extern "C" bool dk64_vita_map_probe_input(uint16_t *,float *,float *);
#endif
static constexpr char data_directory[]="ux0:data/dk64recompiled-probe";
#else
static constexpr char data_directory[]="ux0:data/dk64recompiled";
#endif

int _newlib_heap_size_user = 160 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;
// Vita pthreads otherwise default to 32 KiB. Recompiled call chains and the
// runtime shader compiler execute on std::thread workers, not the main thread.
unsigned int _pthread_stack_default_user = 2 * 1024 * 1024;
#if DK64_VITA_DIAGNOSTICS
static std::atomic<uint8_t *> game_rdram{nullptr};

void vita_log(const char *format, ...) {
    char message[2048]; va_list args; va_start(args,format);
    std::vsnprintf(message,sizeof(message),format,args); va_end(args);
    static const std::string path=std::string(data_directory)+"/progress.log";
    SceUID fd=sceIoOpen(path.c_str(),SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0777);
    if(fd>=0) { sceIoWrite(fd,message,std::strlen(message)); sceIoWrite(fd,"\n",1); sceIoClose(fd); }
}
#endif

extern "C" void recomp_entrypoint(uint8_t *,recomp_context *);
extern gpr get_entrypoint_address();
extern RspUcodeFunc n_aspMain;
std::unique_ptr<ultramodern::renderer::RendererContext> create_vita_renderer(
    uint8_t *,ultramodern::renderer::WindowHandle,bool);

namespace {
    SDL_AudioDeviceID audio_device=0;
    uint32_t audio_rate=0;
    size_t audio_device_frames=0;
    std::mutex audio_mutex;
#if DK64_VITA_AUDIO_CAPTURE
    AudioCapture audio_capture;
    AudioCapture device_capture;
    std::mutex device_capture_mutex;
    int capture_port=-1,capture_frames=0;
#endif
    void message(const char *text) { vita_log("%s",text); }
    void set_frequency(uint32_t hz) {
        std::lock_guard lock(audio_mutex);
#if DK64_VITA_AUDIO_CAPTURE
        audio_capture.set_rate(hz);
#endif
        if(audio_device) SDL_CloseAudioDevice(audio_device);
        SDL_AudioSpec wanted{},actual{};
        wanted.freq=hz; wanted.format=AUDIO_S16SYS; wanted.channels=2; wanted.samples=1024;
        audio_device=SDL_OpenAudioDevice(nullptr,0,&wanted,&actual,0);
        if(!audio_device) throw std::runtime_error(std::string("SDL audio: ")+SDL_GetError());
        audio_rate=actual.freq;audio_device_frames=actual.samples;
        SDL_PauseAudioDevice(audio_device,0);
        vita_log("Audio device opened: %d Hz, %u channels",actual.freq,unsigned(actual.channels));
    }
    void queue_samples(int16_t *samples,size_t count) {
        std::lock_guard lock(audio_mutex);
        if(!audio_device) return;
        std::vector<int16_t> swapped(count);
        // Word-swapped RDRAM reverses the two stereo samples in each word.
        for(size_t i=0;i+1<count;i+=2) { swapped[i]=samples[i+1]; swapped[i+1]=samples[i]; }
#if DK64_VITA_AUDIO_CAPTURE
        audio_capture.append(swapped.data(),swapped.size(),sceKernelGetProcessTimeWide());
#endif
        if(SDL_QueueAudio(audio_device,swapped.data(),swapped.size()*sizeof(int16_t))<0)
            throw std::runtime_error(SDL_GetError());
#if DK64_VITA_DIAGNOSTICS
        static bool reported_audio=false;
        if(!reported_audio) {
            int peak=0; for(int16_t sample:swapped) peak=std::max(peak,std::abs(int(sample)));
            if(peak) { vita_log("First non-silent audio buffer: %u samples, peak=%d",unsigned(count),peak); reported_audio=true; }
        }
#endif
    }
    size_t audio_remaining() {
        std::lock_guard lock(audio_mutex);
        const size_t frames=audio_device ? SDL_GetQueuedAudioSize(audio_device)/(2*sizeof(int16_t)) : 0;
#if DK64_VITA_AUDIO_CAPTURE
        audio_capture.observe_queue(frames,sceKernelGetProcessTimeWide());
#endif
        return vita_audio_frames_remaining(frames,audio_rate,audio_device_frames);
    }
    bool input(int controller,uint16_t *buttons,float *x,float *y) {
        if(controller!=0) return false;
#if DK64_VITA_SCRIPTED_INPUT
        static AdventureProbe probe;
#if DK64_VITA_MAP_PROBE_ENABLED
        if(dk64_vita_map_probe_input(buttons,x,y)) return true;
#endif
        probe.poll(game_rdram.load(),buttons,x,y,DK64_VITA_SCRIPTED_PAUSE);
        return true;
#endif
        SceCtrlData pad{};
        if(sceCtrlPeekBufferPositive(0,&pad,1)<0) return false;
#if DK64_VITA_DIAGNOSTICS
        static bool reported_input=false;
        if(!reported_input) { vita_log("Controller input callback active"); reported_input=true; }
        static unsigned input_polls=0;
        if(++input_polls%120==0) vita_log("Controller polls: %u",input_polls);
#endif
        *buttons=0;
        const std::pair<uint32_t,uint16_t> mapping[]={{SCE_CTRL_CROSS,0x8000},{SCE_CTRL_SQUARE,0x4000},
            {SCE_CTRL_LTRIGGER,0x2000},{SCE_CTRL_START,0x1000},{SCE_CTRL_UP,0x0800},{SCE_CTRL_DOWN,0x0400},
            {SCE_CTRL_LEFT,0x0200},{SCE_CTRL_RIGHT,0x0100},{SCE_CTRL_RTRIGGER,0x0010},
            {SCE_CTRL_TRIANGLE,0x0008},{SCE_CTRL_CIRCLE,0x0002}};
        for(auto pair:mapping) if(pad.buttons&pair.first) *buttons|=pair.second;
        if(pad.rx<64) *buttons|=0x0002;
        if(pad.rx>192) *buttons|=0x0001;
        if(pad.ry<64) *buttons|=0x0008;
        if(pad.ry>192) *buttons|=0x0004;
#if DK64_VITA_DIAGNOSTICS
        static uint16_t last_buttons=0;
        if(*buttons!=last_buttons) { vita_log("Controller buttons: Vita=%08x N64=%04x",pad.buttons,unsigned(*buttons)); last_buttons=*buttons; }
#endif
        *x=(int(pad.lx)-128)/127.0f; *y=(128-int(pad.ly))/127.0f;
        *x=std::clamp(*x,-1.0f,1.0f); *y=std::clamp(*y,-1.0f,1.0f);
        if(std::abs(*x)<0.12f) *x=0;
        if(std::abs(*y)<0.12f) *y=0;
        return true;
    }
    ultramodern::input::connected_device_info_t connected(int controller) {
        using namespace ultramodern::input;
        return {controller==0?Device::Controller:Device::None,Pak::None};
    }
    RspUcodeFunc *rsp_ucode(const OSTask *task) { return task->t.type==M_AUDTASK?&n_aspMain:nullptr; }
    void poll() {}
#if DK64_VITA_AUDIO_CAPTURE
    void dump_audio_capture(const char *name,const AudioCaptureData &captured) {
        if(captured.samples.empty()) return;
        // The capture is complete and immutable before this file I/O begins.
        const std::string prefix=std::string(data_directory)+"/"+name;
        FILE *pcm=std::fopen((prefix+".s16le").c_str(),"wb");
        if(!pcm) throw std::runtime_error("Could not create audio capture");
        const size_t written=std::fwrite(captured.samples.data(),sizeof(int16_t),captured.samples.size(),pcm);
        std::fclose(pcm);
        if(written!=captured.samples.size()) throw std::runtime_error("Audio capture write was incomplete");
        FILE *index=std::fopen((prefix+".csv").c_str(),"w");
        if(!index) throw std::runtime_error("Could not create audio capture index");
        std::fprintf(index,"rate,%u\nreason,%s\ntime_us,first_frame,frames\n",captured.rate,captured.reason);
        for(const auto &chunk:captured.chunks)
            std::fprintf(index,"%llu,%u,%u\n",static_cast<unsigned long long>(chunk.time_us),chunk.first_frame,chunk.frames);
        std::fclose(index);
        if(!captured.queries.empty()) {
            FILE *queries=std::fopen((prefix+"-queue.csv").c_str(),"w");
            if(!queries) throw std::runtime_error("Could not create audio queue capture");
            std::fprintf(queries,"time_us,frames\n");
            for(const auto &query:captured.queries)
                std::fprintf(queries,"%llu,%u\n",static_cast<unsigned long long>(query.time_us),query.frames);
            std::fclose(queries);
        }
        vita_log("Audio capture %s completed: %u Hz, %u stereo frames, %u chunks",
            name,captured.rate,unsigned(captured.samples.size()/2),unsigned(captured.chunks.size()));
    }
#endif
    void update(void *) {
        SDL_PumpEvents();vita_log_guest_profile();
#if DK64_VITA_AUDIO_CAPTURE
        static uint64_t next_check=0;
        const uint64_t now=sceKernelGetProcessTimeWide();
        if(now<next_check) return;
        next_check=now+1000000;
        AudioCaptureData producer,device;
        {
            std::lock_guard lock(audio_mutex);
            if(audio_capture.ready(now)) producer=audio_capture.take();
        }
        {
            std::lock_guard lock(device_capture_mutex);
            if(device_capture.ready(now)) device=device_capture.take();
        }
        dump_audio_capture("audio-capture",producer);
        dump_audio_capture("device-audio-capture",device);
#endif
    }
}

#if DK64_VITA_AUDIO_CAPTURE
extern "C" int __real_sceAudioOutOpenPort(SceAudioOutPortType,int,int,SceAudioOutMode);
extern "C" int __real_sceAudioOutOutput(int,const void *);
extern "C" int __wrap_sceAudioOutOpenPort(SceAudioOutPortType type,int frames,int rate,SceAudioOutMode mode) {
    const int port=__real_sceAudioOutOpenPort(type,frames,rate,mode);
    // The initial dummy 48 kHz device is a main port. Capture the game's BGM
    // stereo port, retaining its actual rate and frame count without changing it.
    if(port>=0 && type==SCE_AUDIO_OUT_PORT_TYPE_BGM && mode==SCE_AUDIO_OUT_MODE_STEREO) {
        std::lock_guard lock(device_capture_mutex);
        capture_port=port;capture_frames=frames;device_capture.set_rate(rate);
        vita_log("Audio capture device: port=%d frames=%d rate=%d stereo",port,frames,rate);
    }
    return port;
}
extern "C" int __wrap_sceAudioOutOutput(int port,const void *buffer) {
    if(buffer) {
        std::lock_guard lock(device_capture_mutex);
        if(port==capture_port)
            device_capture.append(static_cast<const int16_t *>(buffer),size_t(capture_frames)*2,sceKernelGetProcessTimeWide());
    }
    return __real_sceAudioOutOutput(port,buffer);
}
#endif

int main() {
    sceIoMkdir(data_directory,0777);
    std::set_terminate([] {
#if DK64_VITA_DIAGNOSTICS
        try {
            if(auto error=std::current_exception()) std::rethrow_exception(error);
        } catch(const std::exception &e) { vita_log("Unhandled runtime exception: %s",e.what()); }
        catch(...) { vita_log("Unhandled runtime exception of unknown type"); }
#if DK64_VITA_PROFILE_FUNCTIONS
        extern void dump_vita_queue_history();
        dump_vita_queue_history();
#endif
        if(auto *rdram=game_rdram.load()) {
            vita_log("DK64 exception state: code=%u args=%08x,%08x,%08x shutdown=%u watchdog=%d",
                unsigned(MEM_BU(0,0xffffffff807ff01cULL)),unsigned(MEM_W(0,0xffffffff807ff020ULL)),
                unsigned(MEM_W(0,0xffffffff807ff024ULL)),unsigned(MEM_W(0,0xffffffff807ff028ULL)),
                unsigned(MEM_BU(0,0xffffffff80744460ULL)),int(MEM_H(0,0xffffffff8074682cULL)));
            const auto *idle=TO_PTR(OSThread,int32_t(0x80012710));
            vita_log("Idle record: id=%d priority=%d queue=%08x next=%08x context=%p; running queue head=%08x",
                idle->id,idle->priority,uint32_t(idle->queue),uint32_t(idle->next),static_cast<void *>(idle->context),
                uint32_t(ultramodern::thread_queue_peek(rdram,ultramodern::running_queue)));
        }
#endif
        sceKernelExitProcess(1);
    });
#if DK64_VITA_DIAGNOSTICS
    std::freopen((std::string(data_directory)+"/runtime.log").c_str(),"w",stdout);
    std::freopen((std::string(data_directory)+"/error.log").c_str(),"w",stderr);
    std::setvbuf(stdout,nullptr,_IONBF,0); std::setvbuf(stderr,nullptr,_IONBF,0);
#endif
    try {
        // Keep compiled shaders beside this game's data, with a namespace for
        // the pinned vitaGL/vitaShaRK pair and semantic binding mode. Update it
        // when changing those dependencies or the shader compiler options.
        vglSetShaderCachePath((std::string(data_directory)+"/shaders-cd3791e-df24065-pair").c_str());
        // Match Ghostship's Vita clock requests without lowering a higher
        // frequency already selected by the device's performance settings.
        if(scePowerGetArmClockFrequency()<444) scePowerSetArmClockFrequency(444);
        if(scePowerGetBusClockFrequency()<222) scePowerSetBusClockFrequency(222);
        if(scePowerGetGpuClockFrequency()<222) scePowerSetGpuClockFrequency(222);
        if(scePowerGetGpuXbarClockFrequency()<166) scePowerSetGpuXbarClockFrequency(166);
#if DK64_VITA_DIAGNOSTICS
        vita_log("Vita clocks (MHz): CPU=%d bus=%d GPU=%d crossbar=%d",
            scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),
            scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
#endif
        if(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)<0) throw std::runtime_error(SDL_GetError());
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
        recomp::register_config_path(data_directory);
        dk64::register_bk_overlays();
        recomp::GameEntry game{};
        game.rom_hash=0x4d876060f09b3fc5ULL; game.internal_name="DONKEY KONG 64";
        game.display_name="Donkey Kong 64"; game.game_id=u8"DK64"; game.mod_game_id="dk64";
        game.save_type=recomp::SaveType::Eep16k; game.is_enabled=true;
        game.entrypoint_address=get_entrypoint_address(); game.entrypoint=&recomp_entrypoint;
        game.on_init_callback=[](uint8_t *rdram,recomp_context *ctx) {
#if DK64_VITA_DIAGNOSTICS
            game_rdram.store(rdram);
#endif
            dk64::dk_on_init(rdram,ctx);
        };
        recomp::register_game(game);
        char program[]="DK64Recompiled", flag[]="--game", name[]="dk64";
        char *args[]={program,flag,name};
        recomp::Configuration config{};
        config.argc=3; config.argv=args; config.project_version={1,0,2};
        // No native SDL window: the renderer creates Vita's display on its own thread.
        config.gfx_callbacks.create_window=[](void *) -> SDL_Window * { return nullptr; };
        config.rsp_callbacks.get_rsp_microcode=&rsp_ucode;
        config.renderer_callbacks.create_render_context=&create_vita_renderer;
        config.audio_callbacks={&queue_samples,&audio_remaining,&set_frequency};
        config.input_callbacks={&poll,&input,nullptr,&connected};
        config.gfx_callbacks.update_gfx=&update;
        config.error_handling_callbacks.message_box=&message;
        config.message_queue_control.requeue_timer=false;
        vita_log("Starting DK64 Vita runtime");
#if DK64_VITA_SCRIPTED_INPUT
        vita_log("Scripted Adventure validation enabled; separate ROM and saves at %s",data_directory);
#endif
        recomp::start(config);
#if DK64_VITA_DIAGNOSTICS
        game_rdram.store(nullptr);
#endif
        if(audio_device) SDL_CloseAudioDevice(audio_device);
        SDL_Quit();
    } catch(const std::exception &e) { vita_log("DK64 startup failed: %s",e.what()); return 1; }
    return 0;
}
