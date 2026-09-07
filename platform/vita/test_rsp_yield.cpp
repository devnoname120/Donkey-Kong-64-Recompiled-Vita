#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/rsp_yield.hpp"
#include <cstdio>
#include <stdexcept>

extern "C" void osSpTaskYield_recomp(uint8_t *,recomp_context *);
extern "C" void osSpTaskYielded_recomp(uint8_t *,recomp_context *);
namespace {
unsigned requests=0;
bool yielded=false;
uint32_t queried=0;
void check(bool condition,const char *message) {
    if(!condition)throw std::runtime_error(message);
}
void lifecycle() {
    using Task=ultramodern::YieldableGraphicsTask;
    using Start=Task::Start;
    for(unsigned completion_order=0;completion_order<3;++completion_order) {
        Task task;
        check(!task.request_yield() && !task.was_yielded(17),"idle task yielded");
        check(task.start(17)==Start::Execute,"first task did not execute");
        if(completion_order==0) {
            check(task.complete(17),"non-yielded completion lost its SP event");
            check(!task.request_yield() && !task.was_yielded(17),"completed task generated a second SP event");
        } else {
            check(task.request_yield() && !task.request_yield(),"yield acknowledgement was lost or duplicated");
            check(task.was_yielded(17) && !task.was_yielded(18),"yielded state used the wrong task identity");
            if(completion_order==1) {
                check(!task.complete(17),"suspended graphics emitted the audio task's SP completion");
                check(task.was_yielded(17),"finished suspended task released its guest inputs early");
                check(task.start(17)==Start::Complete,"finished task was decoded again on resume");
            } else {
                check(task.start(17)==Start::Resume,"unfinished task was decoded again on resume");
                check(!task.was_yielded(17),"resumed task remained suspended");
                check(task.complete(17),"resumed task lost its final SP completion");
            }
        }
        check(task.start(18)==Start::Execute && task.complete(18),"later task inherited stale state");
    }
    Task task;
    check(task.start(0)==Start::Execute,"zero physical address was treated as idle");
    for(unsigned i=0;i<64;++i) {
        check(task.request_yield(),"repeated audio preemption failed");
        bool rejected=false;
        try { task.start(1); } catch(const std::logic_error &) { rejected=true; }
        check(rejected && task.was_yielded(0),"invalid resume destroyed suspended task");
        check(task.start(0)==Start::Resume,"preempted graphics was submitted twice");
    }
    check(task.complete(0),"repeatedly yielded task never completed");
    bool rejected=false;
    try { task.complete(0); } catch(const std::logic_error &) { rejected=true; }
    check(rejected,"duplicate graphics completion accepted");
}
}
namespace ultramodern {
void submit_rsp_task(uint8_t *,PTR(OSTask)) {}
void yield_rsp_task() { ++requests; }
bool rsp_task_yielded(PTR(OSTask) task) { queried=uint32_t(task);return yielded; }
}
int main() {
    try {
        recomp_context context{};context.r4=int32_t(0x80123450);context.r29=int32_t(0x803ffff0);
        osSpTaskYield_recomp(nullptr,&context);
        check(requests==1,"graphics yield request never reached the runtime");
        yielded=true;osSpTaskYielded_recomp(nullptr,&context);
        check(context.r2==1 && queried==0x80123450,"yielded graphics task was reported completed");
        yielded=false;osSpTaskYielded_recomp(nullptr,&context);
        check(context.r2==0,"completed graphics task was reported yielded");
        check(uint32_t(context.r4)==0x80123450 && uint32_t(context.r29)==0x803ffff0,"yield wrappers changed guest arguments");
        lifecycle();
        std::puts("RSP yield wrappers: request, query, completion and guest argument preservation passed");
        std::puts("RSP lifecycle: completion/yield race orders, no duplicate decode, repeated preemption and delayed input release passed");
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
