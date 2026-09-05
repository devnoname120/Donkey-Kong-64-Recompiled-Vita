#include "blockingconcurrentqueue.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

int _newlib_heap_size_user=32*1024*1024;
unsigned int sceUserMainThreadStackSize=256*1024;
unsigned int _pthread_stack_default_user=256*1024;

static void result(const char *line) {
    SceUID fd=sceIoOpen("ux0:data/rt64-fast/semaphore.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0777);
    if(fd>=0) { sceIoWrite(fd,line,std::strlen(line)); sceIoWrite(fd,"\n",1); sceIoClose(fd); }
}

int main() {
    sceIoMkdir("ux0:data/rt64-fast",0777);
    result("Starting semaphore handoff test");
    // Delayed handoffs exercise kernel waits as well as the atomic fast path.
    moodycamel::LightweightSemaphore a(1),b(0);
    std::atomic<unsigned> owner{0},handoffs{0},finished{0};
    std::atomic<bool> failed{false};
    auto worker=[&](unsigned id,auto &mine,auto &other) {
        for(unsigned i=0;i<20000;++i) {
            if(!mine.wait()) { failed=true; break; }
            if(owner.load()!=id) { failed=true; break; }
            if(i%17==0) sceKernelDelayThread(2000);
            owner.store(1-id); ++handoffs; other.signal();
        }
        ++finished;
    };
    std::thread first([&]{worker(0,a,b);}),second([&]{worker(1,b,a);});
    const uint64_t started=sceKernelGetProcessTimeWide();
    while(finished<2 && !failed && sceKernelGetProcessTimeWide()-started<30000000) sceKernelDelayThread(10000);
    char line[128]; std::snprintf(line,sizeof(line),"Semaphore test: handoffs=%u finished=%u ownership_error=%u",handoffs.load(),finished.load(),unsigned(failed.load()));
    result(line);
    // The whole diagnostic process exits on failure, including a blocked peer.
    if(failed || finished!=2) sceKernelExitProcess(1);
    first.join(); second.join(); result("Semaphore handoff test passed");
    return 0;
}
