#include "recomp.h"
// Include the real VI implementation to advance its private latch deterministically,
// without starting renderer threads or adding a production test-control API.
#include "../../lib/N64ModernRuntime/ultramodern/src/events.cpp"
#include "../../lib/N64ModernRuntime/ultramodern/src/misc_ultra.cpp"
#include "../../lib/N64ModernRuntime/librecomp/src/vi.cpp"
#include <array>
#include <cstdio>
#include <vector>

namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
void latch() {
    std::lock_guard lock{events_context.message_mutex};
    events_context.vi.update_vi();
}
u8 query(uint8_t *rdram) {
    recomp_context ctx{};
    ctx.r4=4;ctx.r29=29;ctx.r31=31;ctx.r2=-1;
    osViGetCurrentMode_recomp(rdram,&ctx);
    check(ctx.r4==4 && ctx.r29==29 && ctx.r31==31,"VI query changed caller state");
    check(ctx.r2<=255,"VI mode result is not an unsigned byte");
    return ctx.r2;
}
}
int main() {
    try {
        std::vector<uint8_t> memory(4*1024*1024);
        uint8_t *rdram=memory.data();
        // A stale libultra context and nonzero padding must not select the mode.
        MEM_W(0,0xffffffff80010190ULL)=0x80018000;
        MEM_W(8,0xffffffff80018000ULL)=0x80019000;
        MEM_B(0,0xffffffff80019000ULL)=123;
        MEM_B(3,0xffffffff80019000ULL)=229;
        check(query(rdram)==0,"VI query read stale guest context before any native mode");
        set_dummy_vi(false);
        check(query(rdram)==0,"pending dummy mode became current before VI latch");
        latch();
        check(query(rdram)==2,"VI query did not read the native dummy mode");
        u8 current=2;
        unsigned index=0;
        for(u8 type:std::array<u8,9>{0,1,2,10,15,25,30,38,255}) {
            const gpr mode=0xffffffff80020000ULL+index++*sizeof(OSViMode);
            MEM_B(0,mode)=type;
            // Native OSViMode reverses its first word to match word-swapped RAM.
            MEM_B(1,mode)=193;MEM_B(2,mode)=194;MEM_B(3,mode)=195;
            MEM_W(4,mode)=0x3202;MEM_W(8,mode)=320;
            recomp_context ctx{};ctx.r4=mode;
            osViSetMode_recomp(rdram,&ctx);
            check(query(rdram)==current,"VI query reported a pending mode before retrace");
            latch();
            check(query(rdram)==type,"VI query did not report the latched guest type byte");
            latch();
            check(query(rdram)==type,"VI mode changed on a repeated latch");
            current=type;
        }
        check(MEM_BU(0,0xffffffff80019000ULL)==123 && MEM_BU(3,0xffffffff80019000ULL)==229,
              "native VI query rewrote the legacy guest mode");
        std::puts("VI modes: native current/pending latch, guest byte order and caller state passed");
        return 0;
    } catch(const std::exception &e) {
        std::fprintf(stderr,"%s\n",e.what());return 1;
    }
}
