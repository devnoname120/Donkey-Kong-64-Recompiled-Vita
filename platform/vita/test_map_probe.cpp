#define DK64_VITA_PROBE_MAP 101
#include "map_probe.cpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr gpr stack=0xffffffff80400000ULL;
unsigned requests=0;
unsigned bonus_requests=0;
int requested_map=101;
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
void ready(uint8_t *rdram) {
    MEM_W(0,0xffffffff8076a0a8ULL)=176;
    MEM_B(0,0xffffffff80755318ULL)=6;MEM_B(0,0xffffffff80755314ULL)=6;
    MEM_B(0,0xffffffff807444ecULL)=0;MEM_B(0,0xffffffff807463b8ULL)=0;
    MEM_W(0,0xffffffff807fbb60ULL)=0;MEM_B(0,0xffffffff8076a0b1ULL)=0;
    MEM_W(0,0xffffffff807fd88cULL)=0;
}
}
void vita_log(const char *,...) {}
extern "C" void func_global_asm_805FF378(uint8_t *rdram,recomp_context *ctx) {
    ++requests;
    check(ctx->r4==gpr(requested_map) && ctx->r5==0,"map probe transition arguments");
    check(ctx->r29==stack-0x20,"map probe private outgoing argument area");
    // Model a callee that uses its argument spill slots and caller-saved FPRs.
    do_sw(rdram,0,ctx->r29,0xdeadbeef);do_sw(rdram,0x10,ctx->r29,0x12345678);
    ctx->r2=123;ctx->r16=456;ctx->f0.u64=0xabcdef;
    ctx->f_odd[0]=0x12345678;ctx->f_odd[8]=0x87654321;
    MEM_W(0,0xffffffff807444e4ULL)=requested_map;MEM_W(0,0xffffffff807444e8ULL)=0;
    MEM_B(0,0xffffffff8076a0b1ULL)=1;
}
extern "C" void func_global_asm_80712774(uint8_t *rdram,recomp_context *ctx) {
    ++bonus_requests;
    func_global_asm_805FF378(rdram,ctx);
    MEM_B(0,0xffffffff80755318ULL)=13;
}
int main() {
    try {
        std::vector<uint8_t> memory(recomp::mem_size);auto *rdram=memory.data();
        struct Gate { gpr address;uint32_t value;bool word; };
        const Gate gates[]={{0xffffffff8076a0a8ULL,80,true},{0xffffffff80755318ULL,5,false},
            {0xffffffff80755314ULL,5,false},{0xffffffff807444ecULL,1,false},
            {0xffffffff807463b8ULL,1,false},{0xffffffff807fbb60ULL,2,true},
            {0xffffffff8076a0b1ULL,1,false},{0xffffffff807fd88cULL,0x3f800000,true},
            {0xffffffff807fd88cULL,0xbf800000,true}};
        for(const auto &gate:gates) {
            ready(rdram);if(gate.word)MEM_W(0,gate.address)=gate.value;else MEM_B(0,gate.address)=gate.value;
            recomp_context ctx{};MapProbe probe;requests=0;
            update_map_probe(probe,rdram,&ctx,101);
            check(!requests && !probe.requested,"map probe bypassed a readiness gate");
        }
        for(unsigned float_mode:{0u,1u}) for(int map:{176,171}) {
            ready(rdram);MEM_W(0,0xffffffff8076a0a8ULL)=map;
            recomp_context ctx{};ctx.r29=stack;ctx.r31=0xffffffff80600000ULL;
            ctx.r2=2;ctx.r16=16;ctx.f0.u64=0x1020304050607080ULL;
            ctx.mips3_float_mode=float_mode;ctx.f_odd=float_mode?&ctx.f1.u32l:&ctx.f0.u32h;
            const recomp_context before=ctx;
            for(unsigned i=0;i<8;++i)MEM_W(i*4,stack)=0x12340000+i;
            MapProbe probe;requests=bonus_requests=0;update_map_probe(probe,rdram,&ctx,101);
            check(requests==1 && probe.requested && !probe.entered,"map probe request state");
            check(bonus_requests==1 && MEM_BU(0,0xffffffff80755318ULL)==13,"map probe omitted bonus-game entry context");
            check(std::memcmp(&ctx,&before,sizeof(ctx))==0,"map probe changed caller registers or FPR aliases");
            for(unsigned i=0;i<8;++i)check(uint32_t(MEM_W(i*4,stack))==0x12340000+i,"map probe overwrote caller stack arguments");
            update_map_probe(probe,rdram,&ctx,101);check(requests==1,"map probe repeated the transition request");
            MEM_W(0,0xffffffff8076a0a8ULL)=101;MEM_H(0,0xffffffff807fbb34ULL)=0;
            update_map_probe(probe,rdram,&ctx,101);check(probe.entered,"map probe did not observe entry");
        }
        {
            ready(rdram);MapProbe probe;recomp_context ctx{};ctx.r29=stack;ctx.f_odd=&ctx.f0.u32h;
            requested_map=7;requests=bonus_requests=0;update_map_probe(probe,rdram,&ctx,7);
            check(requests==1 && bonus_requests==0 && MEM_BU(0,0xffffffff80755318ULL)==6,
                "ordinary map probe entered bonus-game mode");
            requested_map=101;
        }
        check(!probe_pointer(0,4) && !probe_pointer(0x3f000000,4)
            && !probe_pointer(0xffffffff81ffffffULL,4)
            && probe_pointer(0xffffffff80400000ULL,0x180),"map probe pointer bounds");
        constexpr gpr other=0xffffffff80401000ULL,controller=0xffffffff80402000ULL;
        constexpr gpr aad=0xffffffff80403000ULL,info=0xffffffff80404000ULL;
        MEM_W(0,0xffffffff8076a0a8ULL)=101;MEM_H(0,0xffffffff807fbb34ULL)=2;
        MEM_W(0,0xffffffff807fb930ULL)=uint32_t(other);MEM_W(8,0xffffffff807fb930ULL)=uint32_t(controller);
        MEM_W(0x58,other)=124;MEM_W(0x58,controller)=125;
        MEM_W(0x174,controller)=uint32_t(aad);MEM_W(0x178,controller)=uint32_t(info);
        MEM_H(0,aad)=1;MEM_H(0,info)=162;MEM_H(0x14,info)=10;MEM_H(0x16,info)=10;
        const auto before=memory;MapProbe observed;observed.requested=true;recomp_context ctx{};
        update_map_probe(observed,rdram,&ctx,101);
        check(observed.actor==uint32_t(controller) && observed.phase==1 && observed.timer==162
            && observed.remaining==10,"map probe selected the wrong minigame actor");
        check(memory==before,"map probe observation changed guest data");

        // Exercise the actual packet decoder used by the controller callback.
        auto decode=[&](uint32_t packet,uint16_t expected,float x,float y) {
            published_input.store(packet);uint16_t buttons=0;float sx=0,sy=0;
            check(dk64_vita_map_probe_input(&buttons,&sx,&sy),"missing map input override");
            check(buttons==expected && sx==x && sy==y,"map input packet decoded incorrectly");
        };
        const float positions[6][2]={{-0.6f,0.6f},{0,0.6f},{0.6f,0.6f},{-0.6f,-0.6f},{0.6f,-0.6f},{0,-0.6f}};
        MEM_W(4,info)=0x3f800000;MEM_H(0,info)=129;MEM_B(0x23,aad)=5;MEM_B(0x18,aad)=0;
        for(unsigned slot=0;slot<6;++slot) {
            MapProbe shooting;shooting.requested=true;
            MEM_B(13,info)=0x80|slot;MEM_W(0,0xffffffff8076a064ULL)=100;
            const auto untouched=memory;update_map_probe(shooting,rdram,&ctx,101);
            decode(shooting.input,0x8000,positions[slot][0],positions[slot][1]);
            check(memory==untouched,"targeted input changed game data directly");
            MEM_W(0,0xffffffff8076a064ULL)=101;update_map_probe(shooting,rdram,&ctx,101);
            decode(shooting.input,0x8000,positions[slot][0],positions[slot][1]);
            MEM_W(0,0xffffffff8076a064ULL)=102;update_map_probe(shooting,rdram,&ctx,101);
            decode(shooting.input,0,positions[slot][0],positions[slot][1]);
            MEM_W(0,0xffffffff8076a064ULL)=115;MEM_B(0x18,aad)=3;
            update_map_probe(shooting,rdram,&ctx,101);decode(shooting.input,0,positions[slot][0],positions[slot][1]);
            MEM_B(0x18,aad)=0;MEM_H(0,info)=-20;MEM_W(0,0xffffffff8076a064ULL)=116;
            update_map_probe(shooting,rdram,&ctx,101);decode(shooting.input,0,positions[slot][0],positions[slot][1]);
            MEM_H(0,info)=129;
            MEM_W(0,0xffffffff8076a064ULL)=120;update_map_probe(shooting,rdram,&ctx,101);
            decode(shooting.input,0x8000,positions[slot][0],positions[slot][1]);
            MEM_W(0,0xffffffff8076a064ULL)=125;update_map_probe(shooting,rdram,&ctx,101);
            decode(shooting.input,0,positions[slot][0],positions[slot][1]);
        }
        MapProbe reload;reload.requested=true;MEM_B(0x23,aad)=0;MEM_W(0,0xffffffff8076a064ULL)=200;
        update_map_probe(reload,rdram,&ctx,101);decode(reload.input,0x8000,0,0);
        MEM_B(0x23,aad)=5;MEM_W(0,0xffffffff8076a064ULL)=201;
        update_map_probe(reload,rdram,&ctx,101);decode(reload.input,0x8000,0,0);
        MEM_W(0,0xffffffff8076a064ULL)=202;
        update_map_probe(reload,rdram,&ctx,101);decode(reload.input,0,0,-0.6f);
        MEM_B(0x154,controller)=3;update_map_probe(reload,rdram,&ctx,101);
        published_input.store(reload.input);uint16_t buttons=0x4000;float sx=0.25f,sy=-0.25f;
        check(!dk64_vita_map_probe_input(&buttons,&sx,&sy) && buttons==0x4000 && sx==0.25f && sy==-0.25f,
            "inactive probe overwrote fallback controls");
        MEM_B(0x154,controller)=0;MEM_B(13,info)=0xff;update_map_probe(reload,rdram,&ctx,101);
        check(reload.input==0,"invalid target slot produced input");
        std::puts("Map probe readiness, caller state, actor observation, six aiming slots, firing/release, cooldown, reload and fallback checks passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
