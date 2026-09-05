#include "recomp.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

extern "C" void dk64_vita_restore_helm_medals(uint8_t *,recomp_context *,uint32_t);
extern "C" void dk64_vita_remove_cutscene_controllers(uint8_t *,recomp_context *);
extern "C" int dk64_vita_cover_stopped_transition(uint8_t *,recomp_context *);
static bool helm_complete=false;
static unsigned flag_reads=0;
static std::vector<unsigned> flags_written;
static std::vector<gpr> actors_deleted;
static bool compact_actor_list=false;
static unsigned black_covers=0;
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
extern "C" void isFlagSet(uint8_t *,recomp_context *ctx) {
    check(ctx->r29==0xffffffff80400000ULL,"flag query lost the guest stack");
    check(ctx->r4==0x302 && ctx->r5==0,"wrong permanent completion flag");
    ++flag_reads; ctx->r2=helm_complete; ctx->r4=0xdead;
}
extern "C" void setFlag(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r5==1 && ctx->r6==2,"restoration changed the wrong flag bank");
    const unsigned flag=ctx->r4;
    flags_written.push_back(flag);
    MEM_B(flag>>3,0x80002000)=MEM_BU(flag>>3,0x80002000)|(1U<<(flag&7));
    ctx->r2=0xabcdef; ctx->r5=0xdead; ctx->r6=0xbeef;
}
extern "C" void deleteActor(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r29==0xffffffff80400000ULL,"actor cleanup lost the guest stack");
    actors_deleted.push_back(ctx->r4);
    if(compact_actor_list) {
        const unsigned count=MEM_HU(0,0xffffffff807fbb34ULL);
        for(unsigned i=0;i<count;++i) if(gpr(MEM_W(i*8,0xffffffff807fb930ULL))==ctx->r4) {
            MEM_H(0,0xffffffff807fbb34ULL)=count-1;
            MEM_W(i*8,0xffffffff807fb930ULL)=MEM_W((count-1)*8,0xffffffff807fb930ULL);
            break;
        }
    }
    ctx->r2=0; ctx->r4=0xdead;
}
extern "C" void func_global_asm_80703374(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r29==0xffffffff803fffe0ULL,"transition hook did not reserve its own guest argument area");
    check(ctx->r5==0 && ctx->r6==0 && ctx->r7==0 && MEM_W(0x10,ctx->r29)==255,"transition cover was not opaque black");
    ++black_covers; ctx->r2=ctx->r4+0x20; ctx->r4=0xdead;
}
int main() {
    try {
        std::vector<uint8_t> memory(8*1024*1024); auto *rdram=memory.data();
        recomp_context context{};
        context.r2=1; context.r4=0x99; context.r5=1; context.r6=4;
        context.r7=0x1234; context.r29=0xffffffff80400000ULL;
        std::array<unsigned char,sizeof(context)> original{};
        std::memcpy(original.data(),&context,sizeof(context));
        auto unchanged=[&] { check(std::memcmp(&context,original.data(),sizeof(context))==0,"gameplay hook clobbered caller registers"); };
        helm_complete=true;
        dk64_vita_restore_helm_medals(rdram,&context,0);
        check(flag_reads==0 && flags_written.empty(),"new save inherited Helm completion"); unchanged();
        helm_complete=false;
        dk64_vita_restore_helm_medals(rdram,&context,1);
        check(flags_written.empty(),"incomplete Helm save received medals"); unchanged();
        helm_complete=true;
        MEM_B(9,0x80002000)=0x05;
        dk64_vita_restore_helm_medals(rdram,&context,1);
        check(flags_written==std::vector<unsigned>({0x4b,0x4c,0x4d,0x4e,0x4f}),"wrong set of Helm completion flags");
        check(MEM_BU(9,0x80002000)==0xfd,"restoration lost unrelated temporary flags"); unchanged();
        dk64_vita_remove_cutscene_controllers(rdram,&context);
        check(actors_deleted.empty(),"empty actor list was not preserved"); unchanged();
        const std::array<gpr,4> actors={0xffffffff80003000ULL,0xffffffff80004000ULL,0xffffffff80005000ULL,0xffffffff80006000ULL};
        MEM_H(0,0xffffffff807fbb34ULL)=actors.size();
        for(unsigned i=0;i<actors.size();++i) {
            MEM_W(i*8,0xffffffff807fb930ULL)=actors[i];
            MEM_W(i*8+4,0xffffffff807fb930ULL)=0x12345678;
            MEM_W(0x58,actors[i])=(i==1 || i==2)?173:7;
        }
        dk64_vita_remove_cutscene_controllers(rdram,&context);
        check(actors_deleted==std::vector<gpr>({actors[1],actors[2]}),"cutscene cleanup missed a controller or deleted an unrelated actor"); unchanged();
        actors_deleted.clear(); compact_actor_list=true;
        for(unsigned i=0;i<actors.size();++i) MEM_W(0x58,actors[i])=(i==0 || i==3)?173:7;
        dk64_vita_remove_cutscene_controllers(rdram,&context);
        check(actors_deleted==std::vector<gpr>({actors[0],actors[3]}),"immediate deletion skipped the controller moved from the tail");
        check(MEM_HU(0,0xffffffff807fbb34ULL)==2,"immediate deletion removed the wrong number of actors"); unchanged();
        MEM_W(0,0xffffffff807fd888ULL)=std::bit_cast<int32_t>(28.0f);
        check(!dk64_vita_cover_stopped_transition(rdram,&context),"transition cover triggered at the boundary"); unchanged();
        MEM_W(0,0xffffffff807fd888ULL)=std::bit_cast<int32_t>(31.0f);
        MEM_W(0,0xffffffff807fd88cULL)=std::bit_cast<int32_t>(0.25f);
        check(!dk64_vita_cover_stopped_transition(rdram,&context),"transition cover bypassed an active animation"); unchanged();
        MEM_W(0,0xffffffff807fd88cULL)=std::bit_cast<int32_t>(0.0f);
        MEM_W(0x10,context.r29)=0x12345678;
        check(dk64_vita_cover_stopped_transition(rdram,&context)==1 && black_covers==1,"completed transition did not draw its cover");
        check(context.r2==context.r4+0x20 && MEM_W(0x10,context.r29)==0x12345678,"transition result or caller stack was corrupted");
        context.r2=1; unchanged();
        std::puts("Gameplay fixes: Helm flags, controller cleanup, transition cover and caller state passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
