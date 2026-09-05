// Use the actual timer worker. The observer replaces only external delivery;
// zero-queue timers must expire/repeat without generating a queue operation.
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
struct Delivery { int32_t queue,message; };
static std::mutex delivery_mutex;
static std::condition_variable delivery_ready;
static std::vector<Delivery> deliveries;
static uint8_t *test_rdram=nullptr;
void ultramodern::set_native_thread_name(const std::string&) {}
void ultramodern::set_native_thread_priority(ThreadPriority) {}
void ultramodern::enqueue_external_message_src(int32_t queue,int32_t message,bool,EventMessageSource source) {
    if(source!=EventMessageSource::Timer) std::_Exit(4);
    if(message==0x33) {
        // Completion allows the receiver to return and reuse a stack timer.
        // Model immediate reuse before the worker resumes after publishing it.
        const uint64_t reused_interval=93750;
        const int32_t reused_queue=int32_t(0x80003000),reused_message=0x44;
        std::memcpy(test_rdram+0x1088,&reused_interval,8);
        std::memcpy(test_rdram+0x1098,&reused_queue,4);
        std::memcpy(test_rdram+0x109c,&reused_message,4);
    }
    { std::lock_guard lock(delivery_mutex); deliveries.push_back({queue,message}); }
    delivery_ready.notify_one();
}
int main() {
    // The runtime's detached timer worker has process lifetime. End the whole
    // fixture after the checks rather than destroying RAM under that worker.
    std::vector<uint8_t> memory(8*1024*1024);
    auto *rdram=memory.data();
    test_rdram=rdram;
    ultramodern::init_timers(rdram);
    osSetTimer(rdram,int32_t(0x80001000),46875,0,0,0x11);
    osSetTimer(rdram,int32_t(0x80001040),93750,93750,0,0x22);
    osSetTimer(rdram,int32_t(0x80001080),468750,0,int32_t(0x80002000),0x33);
    osSetTimer(rdram,int32_t(0x800010c0),1875000,0,int32_t(0x80002000),0x55);
    osSetTimer(rdram,int32_t(0x80001100),234375,234375,int32_t(0x80004000),0x66);
    std::unique_lock lock(delivery_mutex);
    const bool completed=delivery_ready.wait_for(lock,std::chrono::seconds(2),[] {
        bool marker=false; unsigned repeats=0;
        for(const auto &d:deliveries) { marker|=d.message==0x55; repeats+=d.message==0x66; }
        return marker && repeats>=2;
    });
    unsigned first=0,last=0,periodic=0,unexpected=0;
    for(const auto &d:deliveries) {
        if(d.queue==int32_t(0x80002000) && d.message==0x33) ++first;
        else if(d.queue==int32_t(0x80002000) && d.message==0x55) ++last;
        else if(d.queue==int32_t(0x80004000) && d.message==0x66) ++periodic;
        else ++unexpected;
    }
    const bool correct=completed && first==1 && last==1 && periodic>=2 && unexpected==0;
    if(correct) std::puts("Timer delivery: null queues suppressed; one-shot storage is not reused after completion");
    else std::fprintf(stderr,"Timer delivery violation: completed=%d deliveries=%zu\n",int(completed),deliveries.size());
    std::fflush(stdout); std::fflush(stderr);
    std::_Exit(correct?0:1);
}
