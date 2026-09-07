#include "fps_benchmark_logic.h"
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
}
int main() {
    using namespace VitaBenchmark;
    try {
        auto parse=[](const char *text) { std::istringstream input{text};return Config::read(input); };
        auto config=parse("version=1\nrun=test-001\nscenario=world\nseconds=120\nprofile_every=16\n");
        check(config.run=="test-001" && config.scenario==Scenario::World && config.seconds==120 && config.profileEvery==16,"benchmark configuration was ignored");
        for(const char *bad:{"version=2\nrun=a\n","run=../a\n","run=a\nseconds=0\n","run=a\nscenario=other\n","run=a\nunknown=1\n","run=a\nrun=b\n"}) {
            bool rejected=false;try { parse(bad); } catch(const std::exception &) { rejected=true; }
            check(rejected,"invalid or ambiguous benchmark configuration accepted");
        }
        Scene scene{};scene.map=176;scene.mode=scene.copy=6;scene.render=1;
        Route route{Scenario::World};
        auto input=route.poll(scene,0);
        check(input.x==0 && input.y==0 && route.phase()==Phase::Setup,"moved during cutscene setup");
        scene.render=0;scene.frame=100;
        input=route.poll(scene,1000);
        check(route.phase()==Phase::Idle && input.buttons==0,"idle workload did not begin at a playable frame");
        for(unsigned i=0;i<10;++i)route.poll(scene,1000000+i*1000000);
        check(route.phase()==Phase::Idle,"wall time advanced the measured route");
        scene.frame=220;input=route.poll(scene,12000000);
        check(route.phase()==Phase::Camera && input.buttons==1,"camera segment did not follow simulation progress");
        scene.frame=340;input=route.poll(scene,13000000);
        check(route.phase()==Phase::Walk && input.x!=0,"movement segment missing");
        scene.frame=400;route.poll(scene,14000000);
        check(route.phase()==Phase::Done,"finite route never completed");
        Route changed{Scenario::World};scene.frame=100;changed.poll(scene,0);scene.map=7;changed.poll(scene,1);
        check(changed.phase()==Phase::Invalid,"unexpected map transition silently accepted");
        Route rewind{Scenario::World};scene.map=176;scene.frame=100;rewind.poll(scene,0);scene.frame=90;rewind.poll(scene,1);
        check(rewind.phase()==Phase::Invalid,"simulation rewind silently accepted");
        Route attract{Scenario::Attract};input=attract.poll(scene,50000000);
        check(input.buttons==0 && input.x==0 && input.y==0 && attract.phase()==Phase::Attract,"attract benchmark injects input");
        std::puts("FPS benchmark: bounded configuration, state-gated setup, simulation-driven route and invalid-run detection passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
