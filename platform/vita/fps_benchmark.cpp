#include "fps_benchmark.h"
#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/io/stat.h>
#include <vitaGL.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>
#ifdef DK64_FPS_DEBUG_WATCHDOG
#include <thread>
#endif

namespace VitaBenchmark {
namespace {
constexpr const char *directory="ux0:data/dk64recompiled-benchmark";
constexpr size_t eventLimit=32768,profileLimit=4096;
struct Event {
    uint64_t time=0,duration=0;
    Kind kind=Kind::Game;
    uint32_t sequence=0,a=0,b=0,c=0,d=0;
    Scene scene{};
    Phase phase=Phase::Setup;
};
struct Sample { uint32_t event;RT64::FastProfile::Stats stats; };
Config config;
Route route{Scenario::Attract};
Scene latest{};
Phase phase=Phase::Setup;
std::mutex mutex;
std::vector<Event> events;
std::vector<Sample> samples;
std::array<uint32_t,8> sequences{};
std::array<std::atomic<int>,3> threads{};
std::atomic<bool> enabled{false};
uint64_t origin=0,nextPoll=0,firstInput=0;
uint32_t dropped=0,droppedProfiles=0;
uint32_t displaySwaps=0;
bool exported=false;
int exitCode=1;
#ifdef DK64_FPS_DEBUG_WATCHDOG
std::atomic<unsigned> diagnosticStage{0},diagnosticGames{0},diagnosticInputs{0},diagnosticSpans{0};
#endif

bool writeEvents(const std::string &path,const std::vector<Event> &rows) {
    FILE *file=std::fopen(path.c_str(),"w");
    if(!file)return false;
    bool ok=std::fprintf(file,"time_us,kind,sequence,duration_us,a,b,c,d,phase,map,mode,mode_copy,render,cutscene,scene_timer,game_frame,lag,automatic,paused,x,y,z\n")>0;
    for(const auto &e:rows) {
        const auto &s=e.scene;
        if(std::fprintf(file,"%llu,%u,%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.6f,%.6f,%.6f\n",
            static_cast<unsigned long long>(e.time),unsigned(e.kind),e.sequence,static_cast<unsigned long long>(e.duration),
            e.a,e.b,e.c,e.d,unsigned(e.phase),s.map,s.mode,s.copy,s.render,s.cutscene,s.timer,s.frame,s.lag,s.automatic,s.paused,s.x,s.y,s.z)<0)ok=false;
    }
    if(std::fclose(file)!=0)ok=false;
    return ok;
}
bool writeProfiles(const std::string &path,const std::vector<Sample> &rows) {
    using namespace RT64::FastProfile;
    FILE *file=std::fopen(path.c_str(),"w");
    if(!file)return false;
    bool ok=std::fprintf(file,"event")>0;
    for(const char *name:stageNames)if(std::fprintf(file,",%s_us,%s_calls,%s_items",name,name,name)<0)ok=false;
    for(const char *name:counterNames)if(std::fprintf(file,",%s",name)<0)ok=false;
    if(std::fprintf(file,"\n")<0)ok=false;
    for(const auto &row:rows) {
        if(std::fprintf(file,"%u",row.event)<0)ok=false;
        for(size_t i=0;i<stageCount;++i)
            if(std::fprintf(file,",%llu,%llu,%llu",static_cast<unsigned long long>(row.stats.us[i]),
                static_cast<unsigned long long>(row.stats.calls[i]),static_cast<unsigned long long>(row.stats.items[i]))<0)ok=false;
        for(auto value:row.stats.counters)
            if(std::fprintf(file,",%llu",static_cast<unsigned long long>(value))<0)ok=false;
        if(std::fprintf(file,"\n")<0)ok=false;
    }
    if(std::fclose(file)!=0)ok=false;
    return ok;
}
void append(Event event,const RT64::FastProfile::Stats *profile=nullptr) {
    if(events.size()==eventLimit) { ++dropped;return; }
    if(profile) {
        if(samples.size()<profileLimit)samples.push_back({uint32_t(events.size()),*profile});
        else ++droppedProfiles;
    }
    events.push_back(event);
}
void exportData(uint64_t now) {
    std::vector<Event> rows;
    std::vector<Sample> profiles;
    Phase finalPhase;
    uint32_t swaps=0;
    {
        std::lock_guard lock{mutex};
        enabled.store(false,std::memory_order_release);
        finalPhase=phase;
        swaps=displaySwaps;
        rows.swap(events);profiles.swap(samples);
    }
    const auto prefix=std::string(directory)+"/"+config.run;
    const bool ok=writeEvents(prefix+".csv",rows) && writeProfiles(prefix+"-profile.csv",profiles);
    const bool valid=ok && !dropped && !droppedProfiles && finalPhase!=Phase::Invalid
        && (config.scenario==Scenario::Attract || finalPhase==Phase::Done);
    FILE *file=std::fopen((prefix+".json.part").c_str(),"w");
    bool metadata=file!=nullptr;
    if(file) {
#ifdef RT64_FAST_PROFILE
        constexpr const char *compiledProfile="true";
#else
        constexpr const char *compiledProfile="false";
#endif
        metadata=std::fprintf(file,"{\"schema\":1,\"run\":\"%s\",\"scenario\":\"%s\",\"complete\":true,\"valid\":%s,\"phase\":%u,\"events\":%u,\"profiles\":%u,\"dropped\":%u,\"dropped_profiles\":%u,\"duration_us\":%llu,\"profile_every\":%u,\"compiled_stage_profiling\":%s,\"display_swap_calls\":%u}\n",
            config.run.c_str(),config.scenario==Scenario::Attract?"attract":"world",valid?"true":"false",unsigned(finalPhase),
            unsigned(rows.size()),unsigned(profiles.size()),dropped,droppedProfiles,
            static_cast<unsigned long long>(now-origin),config.profileEvery,compiledProfile,swaps)>0;
        if(std::fclose(file)!=0)metadata=false;
    }
    if(metadata && std::rename((prefix+".json.part").c_str(),(prefix+".json").c_str())!=0)metadata=false;
    exported=true;
    exitCode=valid && metadata?0:2;
    // Unloading GXM while its worker is submitting commands faults inside the
    // driver. The runtime joins that worker before process teardown.
    ultramodern::quit();
}
}
int finishShutdown() {
    if(!exported)return 1;
    const auto path=std::string(directory)+"/"+config.run+"-shutdown.json";
    FILE *file=std::fopen((path+".part").c_str(),"w");
    if(!file)return 2;
    bool ok=std::fprintf(file,"{\"schema\":1,\"run\":\"%s\",\"runtime_joined\":true,\"exit_code\":%d}\n",
        config.run.c_str(),exitCode)>0;
    if(std::fclose(file)!=0)ok=false;
    if(ok && std::rename((path+".part").c_str(),path.c_str())!=0)ok=false;
    return ok?exitCode:2;
}
uint64_t clockNow() { return sceKernelGetProcessTimeWide(); }
void completedSwap() {
    std::lock_guard lock{mutex};
    if(enabled.load(std::memory_order_relaxed))++displaySwaps;
}
void milestone(const char *message) {
    const auto path=std::string(directory)+"/"+config.run+"-startup.txt";
    FILE *file=std::fopen(path.c_str(),"a");
    if(file) {
        std::fprintf(file,"%llu %s\n",static_cast<unsigned long long>(clockNow()-origin),message);
        std::fclose(file);
    }
}
void initialize() {
    sceIoMkdir(directory,0777);
    std::ifstream inputFile{std::string(directory)+"/run.cfg"};
    if(!inputFile)throw std::runtime_error("Missing FPS benchmark run.cfg");
    config=Config::read(inputFile);
    route=Route{config.scenario};
    phase=config.scenario==Scenario::Attract?Phase::Attract:Phase::Setup;
    events.reserve(eventLimit);samples.reserve(profileLimit);
    origin=clockNow();nextPoll=origin;
    FILE *file=std::fopen((std::string(directory)+"/"+config.run+"-started.json").c_str(),"w");
    if(!file)throw std::runtime_error("Cannot write FPS benchmark start receipt");
    bool ok=std::fprintf(file,"{\"schema\":1,\"run\":\"%s\",\"profile_every\":%u,\"seconds\":%u}\n",config.run.c_str(),config.profileEvery,config.seconds)>0;
    if(std::fclose(file)!=0)ok=false;
    if(!ok)throw std::runtime_error("Incomplete FPS benchmark start receipt");
    enabled.store(true,std::memory_order_release);
#ifdef DK64_FPS_DEBUG_WATCHDOG
    std::thread([] {
        for(unsigned n=0;n<30 && enabled.load();++n) {
            sceKernelDelayThread(3000000);
            const auto path=std::string(directory)+"/"+config.run+"-watchdog.log";
            if(FILE *f=std::fopen(path.c_str(),"a")) {
                std::fprintf(f,"time=%llu poll=%u games=%u input=%u spans=%u\n",
                    static_cast<unsigned long long>(clockNow()-origin),diagnosticStage.load(),
                    diagnosticGames.load(),diagnosticInputs.load(),diagnosticSpans.load());
                std::fclose(f);
            }
        }
    }).detach();
#endif
}
void fail(const char *message) noexcept {
    enabled.store(false,std::memory_order_release);
    FILE *file=std::fopen("ux0:data/dk64recompiled-benchmark/failure.txt","w");
    if(file) { std::fprintf(file,"%s\n",message?message:"Unknown benchmark failure");std::fclose(file); }
}
Span::Span(Kind value):kind(value) {
#ifdef DK64_FPS_DEBUG_WATCHDOG
    ++diagnosticSpans;
#endif
    if(!enabled.load(std::memory_order_acquire))return;
    {
        std::lock_guard lock{mutex};
        if(!enabled.load(std::memory_order_relaxed))return;
        scene=latest;phase=VitaBenchmark::phase;
        sequence=++sequences[unsigned(kind)];
    }
    threads[1].store(sceKernelGetThreadId(),std::memory_order_relaxed);
#ifdef RT64_FAST_PROFILE
    sampled=config.profileEvery && sequence%config.profileEvery==0;
    previous=RT64::FastProfile::current;
    RT64::FastProfile::current=sampled?&context:nullptr;
#endif
    started=clockNow();
}
Span::~Span() {
    if(!started)return;
    const auto ended=clockNow();
#ifdef RT64_FAST_PROFILE
    RT64::FastProfile::current=previous;
#endif
    std::lock_guard lock{mutex};
    if(!enabled.load(std::memory_order_relaxed))return;
    Event event{started-origin,ended-started,kind,sequence};
    event.scene=scene;event.phase=phase;event.a=sampled;
    append(event,sampled?&context.stats:nullptr);
}
void gameFrame(uint8_t *rdram) {
#ifdef DK64_FPS_DEBUG_WATCHDOG
    ++diagnosticGames;
#endif
    if(!enabled.load(std::memory_order_acquire) || !rdram)return;
    Scene s;
    s.map=MEM_W(0,0xffffffff8076a0a8ULL);
    s.mode=MEM_BU(0,0xffffffff80755318ULL);s.copy=MEM_BU(0,0xffffffff80755314ULL);
    s.render=MEM_BU(0,0xffffffff807444ecULL);
    s.cutscene=MEM_HU(0,0xffffffff807476f4ULL);s.timer=MEM_HU(0,0xffffffff807476f0ULL);
    s.frame=MEM_W(0,0xffffffff8076a064ULL);s.lag=MEM_W(0,0xffffffff80744478ULL);
    s.automatic=MEM_BU(0,0xffffffff807463b8ULL);s.paused=bool(MEM_W(0,0xffffffff807fbb60ULL)&2);
    const uint32_t actor=MEM_W(0,0xffffffff807fbb4cULL),physical=actor&0x1fffffff;
    if(actor>=0x80000000 && actor<0xc0000000 && physical<recomp::mem_size-0x88) {
        const gpr pointer=static_cast<gpr>(static_cast<int32_t>(actor));
        s.x=std::bit_cast<float>(uint32_t(MEM_W(0x7c,pointer)));
        s.y=std::bit_cast<float>(uint32_t(MEM_W(0x80,pointer)));
        s.z=std::bit_cast<float>(uint32_t(MEM_W(0x84,pointer)));
    }
    threads[0].store(sceKernelGetThreadId(),std::memory_order_relaxed);
    const auto now=clockNow();
    std::lock_guard lock{mutex};
    if(!enabled.load(std::memory_order_relaxed))return;
    latest=s;
    Event event{now-origin,0,Kind::Game,++sequences[unsigned(Kind::Game)]};
    event.scene=s;event.phase=phase;append(event);
}
void input(uint16_t *buttons,float *x,float *y) {
#ifdef DK64_FPS_DEBUG_WATCHDOG
    ++diagnosticInputs;
#endif
    *buttons=0;*x=0;*y=0;
    if(!enabled.load(std::memory_order_acquire))return;
    const auto now=clockNow();
    std::lock_guard lock{mutex};
    if(!firstInput)firstInput=now;
    const auto result=route.poll(latest,now-firstInput);
    phase=route.phase();
    *buttons=result.buttons;*x=result.x;*y=result.y;
}
void poll() {
    static bool first=true;
    const bool startup=first;
    if(first) { first=false;milestone("main poll entered"); }
    if(exported || !enabled.load(std::memory_order_acquire))return;
    const auto now=clockNow();
    if(now<nextPoll)return;
#ifdef DK64_FPS_DEBUG_WATCHDOG
    diagnosticStage=1;
#endif
    nextPoll=now+1000000;
    Phase currentPhase;
    if(startup)milestone("poll acquiring recorder mutex");
    { std::lock_guard lock{mutex};currentPhase=phase; }
#ifdef DK64_FPS_DEBUG_WATCHDOG
    diagnosticStage=2;
#endif
    if(startup)milestone("poll acquired recorder mutex");
    if(now-origin>=uint64_t(config.seconds)*1000000 || currentPhase==Phase::Done || currentPhase==Phase::Invalid) {
#ifdef DK64_FPS_DEBUG_WATCHDOG
        diagnosticStage=3;
#endif
        exportData(now);return;
    }
    threads[2].store(sceKernelGetThreadId(),std::memory_order_relaxed);
    Event clocks{now-origin,0,Kind::Clocks};
    clocks.a=scePowerGetArmClockFrequency();clocks.b=scePowerGetGpuClockFrequency();
    clocks.c=scePowerGetBusClockFrequency();clocks.d=scePowerGetGpuXbarClockFrequency();
    if(startup)milestone("poll read clocks");
    std::array<Event,3> infoRows{};
    unsigned count=0;
    for(unsigned role=0;role<threads.size();++role) {
        const int id=threads[role].load(std::memory_order_relaxed);
        if(!id)continue;
        SceKernelThreadInfo info{};info.size=sizeof(info);
#ifdef DK64_FPS_DEBUG_WATCHDOG
        diagnosticStage=20+role;
#endif
        if(startup)milestone("poll querying thread");
        if(sceKernelGetThreadInfo(id,&info)<0)continue;
#ifdef DK64_FPS_DEBUG_WATCHDOG
        diagnosticStage=30+role;
#endif
        if(startup) {
            char details[128];
            std::snprintf(details,sizeof(details),"thread role=%u id=%d priority=%u affinity=%u",role,id,
                unsigned(info.currentPriority),unsigned(info.currentCpuAffinityMask));
            milestone(details);
        }
        auto &row=infoRows[count++];
        row.time=now-origin;row.kind=Kind::Thread;row.sequence=uint32_t(id);
        row.a=role;row.b=uint32_t(info.runClocks);row.c=uint32_t(info.runClocks>>32);row.d=uint32_t(info.currentPriority);
        row.duration=info.currentCpuAffinityMask;
    }
    std::lock_guard lock{mutex};
    if(!enabled.load(std::memory_order_relaxed))return;
#ifdef DK64_FPS_DEBUG_WATCHDOG
    diagnosticStage=4;
#endif
    clocks.scene=latest;clocks.phase=phase;append(clocks);
    for(unsigned i=0;i<count;++i) { infoRows[i].scene=latest;infoRows[i].phase=phase;append(infoRows[i]); }
    if(startup)milestone("main poll completed");
}
}
extern "C" void __real_dk64_vita_calculate_lag(uint8_t *);
extern "C" void __real_vglSwapBuffers(GLboolean);
extern "C" void __wrap_vglSwapBuffers(GLboolean closeScene) {
    __real_vglSwapBuffers(closeScene);
    VitaBenchmark::completedSwap();
}
extern "C" void __wrap_dk64_vita_calculate_lag(uint8_t *rdram) {
    __real_dk64_vita_calculate_lag(rdram);
    VitaBenchmark::gameFrame(rdram);
}
