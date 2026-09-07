#pragma once
#include <cstdint>
#include <istream>
#include <set>
#include <stdexcept>
#include <string>

namespace VitaBenchmark {
enum class Scenario { Attract, World };
enum class Phase : uint32_t { Setup, Attract, Idle, Camera, Walk, Done, Invalid };
struct Scene {
    uint32_t map=0,mode=0,copy=0,render=0,cutscene=0,timer=0,frame=0,lag=0,automatic=0,paused=0;
    float x=0,y=0,z=0;
};
struct Input { uint16_t buttons=0;float x=0,y=0; };
struct Config {
    std::string run;
    Scenario scenario=Scenario::Attract;
    uint32_t seconds=120,profileEvery=16;
    static Config read(std::istream &input) {
        Config config;
        std::set<std::string> seen;
        std::string line;
        while(std::getline(input,line)) {
            if(!line.empty() && line.back()=='\r')line.pop_back();
            if(line.empty())continue;
            const auto split=line.find('=');
            if(split==std::string::npos)throw std::invalid_argument("Malformed benchmark configuration");
            const auto key=line.substr(0,split),value=line.substr(split+1);
            if(!seen.insert(key).second)throw std::invalid_argument("Duplicate benchmark setting");
            auto number=[&] {
                if(value.empty() || value.find_first_not_of("0123456789")!=std::string::npos)
                    throw std::invalid_argument("Invalid benchmark integer");
                const auto n=std::stoul(value);
                if(n>UINT32_MAX)throw std::out_of_range("Benchmark integer overflow");
                return uint32_t(n);
            };
            if(key=="version") {
                if(number()!=1)throw std::invalid_argument("Unsupported benchmark schema");
            } else if(key=="run")config.run=value;
            else if(key=="scenario") {
                if(value=="world")config.scenario=Scenario::World;
                else if(value=="attract")config.scenario=Scenario::Attract;
                else throw std::invalid_argument("Unknown benchmark scenario");
            } else if(key=="seconds")config.seconds=number();
            else if(key=="profile_every")config.profileEvery=number();
            else throw std::invalid_argument("Unknown benchmark setting");
        }
        if(!seen.count("version") || config.run.empty() || config.run.size()>48
            || config.run.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos
            || config.seconds<15 || config.seconds>240 || config.profileEvery>1024)
            throw std::invalid_argument("Benchmark configuration outside safe bounds");
        return config;
    }
};
class Route {
    Scenario scenario;
    Phase state=Phase::Setup;
    uint32_t start=0,last=0,map=0;
public:
    explicit Route(Scenario value):scenario(value) {}
    Phase phase() const { return state; }
    Input poll(const Scene &scene,uint64_t elapsed) {
        Input input;
        if(scenario==Scenario::Attract) { state=Phase::Attract;return input; }
        if(state==Phase::Done || state==Phase::Invalid)return input;
        if(state==Phase::Setup) {
            if(scene.mode!=6 || scene.copy!=6 || scene.render || scene.automatic || scene.paused || !scene.frame) {
                const bool pulse=elapsed>=1000000 && (elapsed-1000000)%4000000<1000000;
                if(pulse)input.buttons=(scene.mode==5 || scene.mode==6)?0x8000:0x1000;
                return input;
            }
            start=last=scene.frame;map=scene.map;state=Phase::Idle;
        }
        if(scene.map!=map || scene.mode!=6 || scene.copy!=6 || scene.render || scene.automatic
            || scene.paused || scene.frame<last) {
            state=Phase::Invalid;return {};
        }
        last=scene.frame;
        const uint32_t frame=scene.frame-start;
        if(frame<120)state=Phase::Idle;
        else if(frame<240) {
            state=Phase::Camera;
            input.buttons=frame<180?1:2;
        } else if(frame<300) {
            state=Phase::Walk;
            const auto direction=((frame-240)/60)%4;
            if(direction<2)input.x=direction==0?0.55f:-0.55f;
            else input.y=direction==2?0.55f:-0.55f;
        } else state=Phase::Done;
        return input;
    }
};
}
