#include "architecture_resources.h"
#include "architecture_specialization.h"
#include <array>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition,const char *message) { if(!condition)throw std::runtime_error(message); }
void resources() {
    ArchitectureProbe::Resources r;
    std::array<uint8_t,16> bytes{};
    r.observe(0x1000,bytes.data(),16);
    bytes[7]=1;
    check(r.data.at({0x1000,16})[7]==0,"Observer did not retain an independent byte snapshot");
    r.observe(0x1000,bytes.data(),16);
    r.observe(0x1000,bytes.data(),16);
    r.observe(0x2000,bytes.data(),16);
    r.observe(0x1000,bytes.data(),8);
    r.observe(0,nullptr,0);
    check(r.calls==6 && r.bytes==72 && r.hits==1 && r.hitBytes==16 && r.changes==1,
        "Resource reuse counters conflate first observations, changes and exact hits");
    check(r.data.size()==3 && r.stored==40,"Range identity omitted address or length");
    r.stored=8*1024*1024;
    r.observe(0x3000,bytes.data(),16);
    check(r.capped && r.data.size()==3,"Resource retention exceeded its budget");
    r.observe(0x1000,bytes.data(),16);
    check(r.hits==2,"Retention cap disabled comparisons of already retained ranges");
}
void specialization() {
    using namespace ArchitectureProbe;
    RT64::FastDraw draw;
    auto texture=std::make_shared<RT64::FastTexture>();
    texture->width=32;texture->height=16;
    draw.textures[0]=texture;
    auto &tile=draw.tiles[0];
    tile.cms=G_TX_MIRROR;tile.cmt=G_TX_CLAMP;tile.masks=5;
    tile.shifts=12;tile.shiftt=3;tile.uls=6;tile.ult=10;tile.lrs=130;tile.lrt=70;
    const auto source=specializeSampler(draw);
    check(source.find("const vec2 uSize0=vec2(32.0,16.0);")!=std::string::npos,"Texture dimensions were not specialized");
    check(source.find("const vec4 uTile0=vec4(16.0,0.125,1.5,2.5);")!=std::string::npos,"Tile descriptor conversion changed");
    check(source.find("const vec2 uMask0=vec2(32.0,0.0);")!=std::string::npos,"Mask specialization changed");
    check(source.find("const vec2 uMirror0=vec2(1.0,0.0);")!=std::string::npos,"Mirror specialization changed");
    check(source.find("uniform sampler2D uTex0;")!=std::string::npos,"Specialization replaced the image instead of its descriptor");
    check(source.find("uniform vec2 uSize0;")==std::string::npos,"Original descriptor declaration remained live");
    const auto same=specializeSampler(draw);check(source==same,"Specialization is nondeterministic");
    tile.shifts=11;check(source!=specializeSampler(draw),"Different descriptor reused the same shader");
    texture->storage=std::make_shared<RT64::FastTextureStorage>();
    bool rejected=false;
    try {specializeSampler(draw);}catch(const std::runtime_error &){rejected=true;}
    check(rejected,"Frozen specialization accepted a framebuffer-backed texture");
    draw.fill=true;check(specializeSampler(draw)==RT64::fastFragmentShader(draw),"Fill shader changed");
    std::string duplicate="same same";rejected=false;
    try {substitute(duplicate,"same","other");}catch(const std::runtime_error &){rejected=true;}
    check(rejected && duplicate=="same same","Ambiguous shader substitution was accepted");
}
}
int main() {
    try {resources();specialization();std::puts("Architecture helpers: exact source reuse, bounds and sampler specialization passed");return 0;}
    catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
