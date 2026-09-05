#pragma once
#include "fast/rt64_fast.h"

inline void trace_fast_draw(const RT64::FastDraw &d,unsigned number,void (*log)(const char *,...)) {
    log("Draw trace %u image=%08x depth=%08x %ux%u mux=%08x/%08x mode=%08x/%08x rect=%d fill=%d clear=%d z=%d/%d cull=%d/%d fog=%d",
        number,d.colorAddress,d.depthAddress,d.width,d.height,d.combine.L,d.combine.H,d.otherMode.H,d.otherMode.L,
        d.rectangle,d.fill,d.clearDepth,d.depthTest,d.depthWrite,d.cullFront,d.cullBack,d.fog);
    log("Color trace prim=%.3f,%.3f,%.3f,%.3f env=%.3f,%.3f,%.3f,%.3f fill=%.3f,%.3f,%.3f,%.3f scissor=%d,%d,%d,%d",
        d.primitive[0],d.primitive[1],d.primitive[2],d.primitive[3],d.environment[0],d.environment[1],d.environment[2],d.environment[3],
        d.fillColor[0],d.fillColor[1],d.fillColor[2],d.fillColor[3],d.scissor[0],d.scissor[1],d.scissor[2],d.scissor[3]);
    for(unsigned i=0;i<2;++i) if(d.textures[i]) {
        const auto &t=*d.textures[i]; const auto &tile=d.tiles[i];
        log("Texture trace %u hash=%016llx %ux%u fmt=%u/%u clamp=%u/%u mask=%u/%u shift=%u/%u bounds=%u,%u,%u,%u",
            i,static_cast<unsigned long long>(t.hash),t.width,t.height,tile.fmt,tile.siz,tile.cms,tile.cmt,tile.masks,tile.maskt,
            tile.shifts,tile.shiftt,tile.uls,tile.ult,tile.lrs,tile.lrt);
    }
    for(unsigned i=0;i<d.vertices.size()&&i<3;++i) {
        const auto &v=d.vertices[i];
        log("Vertex trace %u clip=%.3f,%.3f,%.3f,%.3f uv=%.3f,%.3f shade=%.3f,%.3f,%.3f,%.3f fog=%.3f",
            i,v.position[0],v.position[1],v.position[2],v.position[3],v.uv[0],v.uv[1],v.color[0],v.color[1],v.color[2],v.color[3],v.fog);
    }
}
