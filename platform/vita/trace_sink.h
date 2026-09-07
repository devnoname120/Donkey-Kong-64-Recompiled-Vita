#pragma once
#include "draw_trace.h"
#include "log.h"
#include <cstdio>
#include <string>
#include <unordered_set>

class TraceSink final : public RT64::FastDrawSink {
    std::unique_ptr<RT64::FastDrawSink> backend;
    std::string directory;
    std::unordered_set<uint64_t> textures;
    unsigned draw_reports=0;
public:
    TraceSink(std::unique_ptr<RT64::FastDrawSink> sink,const char *data_directory)
        : backend(std::move(sink)),directory(data_directory) {}
    void startTask(uint64_t task) {
        if(task==480) { draw_reports=0; textures.clear(); vita_log("Capturing render task 480"); }
    }
    void draw(const RT64::FastDraw &draw) override {
        if(draw_reports<14) trace_fast_draw(draw,++draw_reports,vita_log);
        for(unsigned i=0;i<2;++i) if(draw.textures[i] && textures.size()<8) {
            const auto &texture=*draw.textures[i];
            if(textures.insert(texture.hash).second) {
                vita_log("Decoded texture %016llx %ux%u fmt=%u siz=%u",static_cast<unsigned long long>(texture.hash),
                    texture.width,texture.height,draw.tiles[i].fmt,draw.tiles[i].siz);
                if(texture.storage)continue;
                char name[48];std::snprintf(name,sizeof(name),"/texture-%016llx.rgba",static_cast<unsigned long long>(texture.hash));
                FILE *file=std::fopen((directory+name).c_str(),"wb");
                if(file) {
                    const size_t written=std::fwrite(texture.rgba.data(),1,texture.rgba.size(),file);
                    const int closed=std::fclose(file);
                    if(written!=texture.rgba.size() || closed)vita_log("Texture capture write failed");
                }
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
    bool readDepthFramebuffer(uint32_t address,uint32_t size,std::vector<uint8_t> &bytes) override {
        return backend->readDepthFramebuffer(address,size,bytes);
    }
};
