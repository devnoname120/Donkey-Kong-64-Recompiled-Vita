#include "architecture_probe.h"
#include "architecture_resources.h"
#include "architecture_specialization.h"
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include "xxHash/xxh3.h"

namespace ArchitectureProbe {
namespace {
uint64_t now() { return sceKernelGetProcessTimeWide(); }
uint32_t word(const uint8_t *p,uint32_t a) { uint32_t v;std::memcpy(&v,p+(a&0x1fffffff),4);return v; }
uint16_t half(const uint8_t *p,uint32_t a) { uint16_t v;std::memcpy(&v,p+((a&0x1fffffff)^2),2);return v; }
uint8_t byte(const uint8_t *p,uint32_t a) { return p[(a&0x1fffffff)^3]; }
struct Result { unsigned mode,trial;uint64_t submit,complete; };
class ReplaySink;
ReplaySink *active=nullptr;
bool observing=false,resident=false;
unsigned replayMode=0,replayWidth=0,replayHeight=0;
std::map<const RT64::FastVertex *,GLuint> geometry;
Resources vertexResources,textureResources;
Resources commandResources;
std::map<uint32_t,uint64_t> commandVisits;
uint64_t observationEpoch=0,commands=0;
uint64_t operations[8]{};
uint64_t vertexFeatures[4]{};
std::map<const RT64::FastDraw *,std::pair<unsigned,std::string>> specialized;
unsigned specializedCount=0;
class ReplaySink final : public RT64::FastDrawSink {
    std::unique_ptr<RT64::FastDrawSink> backend;
    std::vector<RT64::FastDraw> draws;
    std::vector<int> order;
    std::vector<Result> results;
    bool recording=false,finished=false,feedback=false;
    unsigned worldFrames=0,observedFrames=0;
    uint32_t selectedMap=0,selectedTimer=0,colorAddress=0,width=0,height=0;
    std::string run;
    bool specializedExact=true;
    std::vector<uint8_t> readImage() {
        glFinish();const size_t size=size_t(width)*height*4;
        void *mapped=vglMemalign(64,size);if(!mapped)throw std::bad_alloc();
        vglReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,mapped);
        const auto error=glGetError();
        std::vector<uint8_t> bytes(static_cast<uint8_t *>(mapped),static_cast<uint8_t *>(mapped)+size);
        vglFree(mapped);
        if(error!=GL_NO_ERROR)throw std::runtime_error("Architecture color read failed");
        return bytes;
    }
    void replay() {
        for(int op:order) { if(op<0)backend->fullSync();else backend->draw(draws.at(size_t(op))); }
        backend->fullSync();
    }
    void cleanup() {
        resident=false;replayMode=0;glFinish();
        for(const auto &entry:geometry)glDeleteBuffers(1,&entry.second);
        geometry.clear();
    }
    void report(bool exact,const std::vector<uint8_t> &image,const char *error="") {
        char path[256];std::snprintf(path,sizeof(path),"ux0:data/dk64recompiled-benchmark/%s-architecture.json",run.c_str());
        FILE *f=std::fopen(path,"w");if(!f)throw std::runtime_error("Architecture report creation failed");
        uint64_t vertexCount=0;std::set<const RT64::FastTexture *> textures;
        for(const auto &d:draws) { vertexCount+=d.vertices.size();for(const auto &t:d.textures)if(t)textures.insert(t.get()); }
        std::fprintf(f,"{\"schema\":1,\"run\":\"%s\",\"diagnostic_not_game_fps\":true,\"map\":%u,\"scene_timer\":%u,\"observed_frames\":%u,\"draw_packets\":%u,\"vertex_records\":%llu,\"vertex_bytes\":%llu,\"texture_objects\":%u,\"feedback_dependency\":%s,\"replay_exact_rgba\":%s,\"image_hash\":\"%016llx\",\"error\":\"%s\",\"vertex_resources\":",
            run.c_str(),selectedMap,selectedTimer,observedFrames,unsigned(draws.size()),(unsigned long long)vertexCount,
            (unsigned long long)(vertexCount*sizeof(RT64::FastVertex)),unsigned(textures.size()),feedback?"true":"false",exact?"true":"false",
            (unsigned long long)XXH3_64bits(image.data(),image.size()),error);
        vertexResources.write(f);std::fprintf(f,",\"texture_resources\":");textureResources.write(f);
        std::set<uint32_t> colors;
        for(size_t i=0;i+4<=image.size();i+=4) { uint32_t pixel;std::memcpy(&pixel,image.data()+i,4);colors.insert(pixel); }
        std::fprintf(f,",\"width\":%u,\"height\":%u,\"unique_rgba_colors\":%u,\"specialized_exact_rgba\":%s,\"specialized_programs\":%u,\"commands\":%llu,\"command_page_resources\":",width,height,unsigned(colors.size()),specializedExact?"true":"false",specializedCount,(unsigned long long)commands);
        commandResources.write(f);std::fprintf(f,",\"operations\":[");
        for(unsigned i=0;i<8;++i)std::fprintf(f,"%s%llu",i?",":"",(unsigned long long)operations[i]);
        std::fprintf(f,"],\"vertex_features\":[");
        for(unsigned i=0;i<4;++i)std::fprintf(f,"%s%llu",i?",":"",(unsigned long long)vertexFeatures[i]);
        std::fprintf(f,"]");
        std::fprintf(f,",\"samples\":[");
        for(size_t i=0;i<results.size();++i) { const auto &r=results[i];
            std::fprintf(f,"%s{\"mode\":%u,\"trial\":%u,\"submit_us\":%llu,\"complete_us\":%llu}",i?",":"",r.mode,r.trial,(unsigned long long)r.submit,(unsigned long long)r.complete); }
        std::fprintf(f,"]}\n");std::fclose(f);
        if(!image.empty()) {
            std::snprintf(path,sizeof(path),"ux0:data/dk64recompiled-benchmark/%s-architecture.ppm",run.c_str());
            FILE *ppm=std::fopen(path,"wb");if(!ppm)throw std::runtime_error("Architecture image creation failed");
            std::fprintf(ppm,"P6\n%u %u\n255\n",width,height);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)std::fwrite(image.data()+((height-1-y)*width+x)*4,1,3,ppm);
            std::fclose(ppm);
        }
    }
public:
    explicit ReplaySink(std::unique_ptr<RT64::FastDrawSink> sink):backend(std::move(sink)) {
        FILE *file=std::fopen("ux0:data/dk64recompiled-benchmark/run.cfg","r");
        if(!file)throw std::runtime_error("Architecture probe requires benchmark configuration");
        char line[160];while(std::fgets(line,sizeof(line),file))if(!std::strncmp(line,"run=",4)) {
            run=line+4;run.erase(run.find_last_not_of("\r\n")+1);
        }
        std::fclose(file);
        if(run.empty() || run.size()>48 || run.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
            throw std::runtime_error("Architecture probe invalid run identifier");
    }
    ~ReplaySink() override { active=nullptr; }
    void begin(const uint8_t *p) {
        if(finished)return;
        const uint32_t map=word(p,0x8076a0a8);const unsigned mode=byte(p,0x80755318);
        const unsigned cutscene=half(p,0x807476f4),timer=half(p,0x807476f0);
        const bool rap=map==76 && mode==2 && cutscene==7 && timer>=120;
        const bool world=map==34 && mode==6 && !byte(p,0x807444ec) && !byte(p,0x807463b8) && !(word(p,0x807fbb60)&2);
        if(world)++worldFrames;
        observing=rap || world;if(observing)++observedFrames;
        ++observationEpoch;
        if((rap && timer>=240) || (world && worldFrames==75)) { recording=true;selectedMap=map;selectedTimer=timer; }
    }
    void end() {
        if(!recording || finished)return;
        recording=false;observing=false;finished=true;
        if(feedback || draws.empty() || !width || !height) { report(false,{},"unsupported replay dependency");return; }
        std::vector<uint8_t> original;
        try {
            original=readImage();
            std::set<uint32_t> unique;
            for(size_t i=0;i+4<=original.size();i+=4) { uint32_t v;std::memcpy(&v,original.data()+i,4);unique.insert(v); }
            if(unique.size()<16)throw std::runtime_error("Architecture image lacks visual content");
            replayWidth=width;replayHeight=height;
            std::map<std::string,unsigned> programs;
            for(const auto &d:draws)if(!d.vertices.empty()) {
                GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
                glBufferData(GL_ARRAY_BUFFER,d.vertices.size()*sizeof(RT64::FastVertex),d.vertices.data(),GL_STATIC_DRAW);
                geometry.emplace(d.vertices.data(),buffer);
                if(!d.fill) {
                    auto shader=specializeSampler(d);auto found=programs.find(shader);
                    if(found==programs.end())found=programs.emplace(shader,unsigned(programs.size()+1)).first;
                    specialized.emplace(&d,std::make_pair(found->second,std::move(shader)));
                }
            }
            specializedCount=unsigned(programs.size());
            for(unsigned warm=0;warm<10;++warm) { replayMode=warm%5;resident=replayMode!=0;replay();glFinish(); }
            replayMode=0;resident=false;replay();glFinish();
            bool exact=readImage()==original;
            for(unsigned trial=0;trial<24;++trial)for(unsigned step=0;step<5;++step) {
                const unsigned mode=(trial+step)%5;replayMode=mode;resident=mode!=0;
                glFinish();const auto start=now();replay();const auto submitted=now();glFinish();const auto complete=now();
                results.push_back({mode,trial,submitted-start,complete-start});
                if(mode<2 && (trial==0 || trial==23))exact=exact && readImage()==original;
                if(mode==4 && (trial==0 || trial==23))specializedExact=specializedExact && readImage()==original;
            }
            replayMode=0;resident=false;replay();glFinish();exact=exact && readImage()==original;
            cleanup();report(exact,original);
        } catch(...) { cleanup();report(false,original,"replay experiment failed");throw; }
        draws.clear();order.clear();
    }
    void draw(const RT64::FastDraw &d) override {
        if(recording) {
            if(draws.size()>=4096)throw std::runtime_error("Architecture packet limit exceeded");
            for(const auto &t:d.textures)if(t && t->storage)feedback=true;
            if(!d.clearDepth) {
                if(width && (colorAddress!=d.colorAddress || width!=d.width || height!=d.height))feedback=true;
                colorAddress=d.colorAddress;width=d.width;height=d.height;
            }
            order.push_back(int(draws.size()));draws.push_back(d);
        }
        backend->draw(d);
    }
    void fullSync() override { if(recording)order.push_back(-1);backend->fullSync(); }
    void flushDraws() override { backend->flushDraws(); }
    void present(uint32_t address) override { backend->present(address); }
    void present(const RT64::VI &vi) override { backend->present(vi); }
    void setRDRAM(const uint8_t *p,size_t size) override { backend->setRDRAM(p,size); }
    void setMemoryWriteTracking(std::function<void(uint32_t,uint32_t,bool)> watch) override { backend->setMemoryWriteTracking(std::move(watch)); }
    void notifyMemoryWrites(const std::vector<RT64::FastMemoryWrite> &writes) override { if(recording && !writes.empty())feedback=true;backend->notifyMemoryWrites(writes); }
    bool maySnapshotFramebuffer(uint32_t a,uint32_t size) const override { return backend->maySnapshotFramebuffer(a,size); }
    std::shared_ptr<const RT64::FastFramebuffer> snapshotFramebuffer(uint32_t a,uint32_t size) override {
        auto result=backend->snapshotFramebuffer(a,size);if(recording && result)feedback=true;return result;
    }
    bool readFramebufferSnapshot(const RT64::FastFramebuffer &frame,std::vector<uint8_t> &bytes) override { if(recording)feedback=true;return backend->readFramebufferSnapshot(frame,bytes); }
    bool readFramebuffer(uint32_t a,uint32_t size,std::vector<uint8_t> &bytes) override { if(recording)feedback=true;return backend->readFramebuffer(a,size,bytes); }
    bool readDepthFramebuffer(uint32_t a,uint32_t size,std::vector<uint8_t> &bytes) override { if(recording)feedback=true;return backend->readDepthFramebuffer(a,size,bytes); }
};
}
std::unique_ptr<RT64::FastDrawSink> createSink() {
    auto recorder=std::make_unique<ReplaySink>(RT64::createFastVitaGLSink(false,false));
    active=recorder.get();return RT64::createFastBatchingSink(std::move(recorder));
}
void beginTask(const uint8_t *p) { if(active)active->begin(p); }
void endTask() { if(active)active->end(); }
void observeVertex(uint32_t a,const uint8_t *p,uint32_t size,uint32_t flags) {
    if(!observing)return;
    vertexResources.observe(a,p,size);
    const uint32_t feature[]={G_LIGHTING,G_TEXTURE_GEN,G_TEXTURE_GEN_LINEAR,G_FOG};
    for(unsigned i=0;i<4;++i)if(flags&feature[i])vertexFeatures[i]+=size/16;
}
void observeTexture(uint32_t a,const uint8_t *p,uint32_t size) { if(observing)textureResources.observe(a,p,size); }
void observeOperation(unsigned index) { if(observing && index<8)++operations[index]; }
void observeCommand(uint32_t address,const uint8_t *rdram,size_t size) {
    if(!observing)return;
    ++commands;const uint32_t page=address&~255U;auto &epoch=commandVisits[page];
    if(epoch!=observationEpoch) { epoch=observationEpoch;commandResources.observe(page,rdram+page,uint32_t(std::min<size_t>(256,size-page))); }
}
unsigned residentGeometry(const std::vector<RT64::FastVertex> &v) {
    if(!resident)return 0;
    const auto found=geometry.find(v.data());return found==geometry.end()?0:found->second;
}
void configureViewport() { if(replayMode==2)glViewport(0,0,replayWidth/2,replayHeight/2); }
bool simpleFragment() { return replayMode==3; }
unsigned specializationId(const RT64::FastDraw &draw) {
    if(replayMode!=4)return 0;
    const auto found=specialized.find(&draw);return found==specialized.end()?0:found->second.first;
}
const std::string &specializedSource(const RT64::FastDraw &draw) { return specialized.at(&draw).second; }
}
