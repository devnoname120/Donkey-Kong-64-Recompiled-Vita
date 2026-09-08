#pragma once
#include "fast/rt64_fast.h"
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
namespace ArchitectureProbe {
inline std::string scalar(float value) {
    std::ostringstream out;out<<std::setprecision(std::numeric_limits<float>::max_digits10)<<value;
    std::string result=out.str();if(result.find_first_of(".eE")==std::string::npos)result+=".0";return result;
}
inline std::string vec2(float x,float y) { return "vec2("+scalar(x)+","+scalar(y)+")"; }
inline std::string vec4(float x,float y,float z,float w) { return "vec4("+scalar(x)+","+scalar(y)+","+scalar(z)+","+scalar(w)+")"; }
inline void substitute(std::string &source,const std::string &old,const std::string &replacement) {
    const size_t at=source.find(old);
    if(at==std::string::npos || source.find(old,at+old.size())!=std::string::npos)throw std::runtime_error("Sampler specialization declaration changed");
    source.replace(at,old.size(),replacement);
}
inline std::string specializeSampler(const RT64::FastDraw &draw) {
    std::string source=RT64::fastFragmentShader(draw);
    if(draw.fill)return source;
    for(unsigned i=0;i<2;++i)if(draw.textures[i]) {
        const auto &image=*draw.textures[i];const auto &tile=draw.tiles[i];
        if(image.storage)throw std::runtime_error("Frozen sampler prototype excludes framebuffer textures");
        const auto index=std::to_string(i);
        auto constant=[&](const char *type,const char *name,const std::string &value) {
            const auto uniform=std::string(name)+index;
            substitute(source,std::string("uniform ")+type+" "+uniform+";",std::string("const ")+type+" "+uniform+"="+value+";");
        };
        constant("vec2","uSize",vec2(image.width,image.height));
        substitute(source,"uniform vec2 uImageOrigin"+index+", uImageSign"+index+";",
            "const vec2 uImageOrigin"+index+"=vec2(0.0);\nconst vec2 uImageSign"+index+"=vec2(1.0);");
        auto shift=[](unsigned value){return value<=10?1.0f/float(1U<<value):float(1U<<(16-value));};
        constant("vec4","uTile",vec4(shift(tile.shifts),shift(tile.shiftt),tile.uls/4.0f,tile.ult/4.0f));
        constant("vec2","uClamp",vec2((tile.cms&G_TX_CLAMP)||!tile.masks?((tile.lrs-tile.uls)&4095)/4.0f:-1,
            (tile.cmt&G_TX_CLAMP)||!tile.maskt?((tile.lrt-tile.ult)&4095)/4.0f:-1));
        constant("vec2","uMask",vec2(tile.masks?float(1U<<tile.masks):0,tile.maskt?float(1U<<tile.maskt):0));
        constant("vec2","uMirror",vec2(tile.cms&G_TX_MIRROR?1:0,tile.cmt&G_TX_MIRROR?1:0));
    }
    return source;
}
}
