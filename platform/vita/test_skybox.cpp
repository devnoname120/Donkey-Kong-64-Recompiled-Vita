#include "recomp.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_80707980(uint8_t *,recomp_context *);
namespace {
constexpr gpr display_list=0xffffffff80300000ULL,stack=0xffffffff80600000ULL;
constexpr gpr character=0xffffffff80400000ULL,player=0xffffffff80401000ULL,matrix=0xffffffff80402000ULL;
unsigned skies=0,black_fills=0,textured_skies=0;
int expected_tint=0;
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
struct Fixture {
    std::vector<uint8_t> memory=std::vector<uint8_t>(8*1024*1024);
    uint8_t *rdram=memory.data();
    recomp_context ctx{};
    Fixture(int map,int chunk,int view,int override) {
        skies=black_fills=textured_skies=0;
        expected_tint=view==3 || view==9 || view==11;
        ctx.f_odd=&ctx.f0.u32h; ctx.r29=stack; ctx.r31=0xffffffff80500000ULL;
        ctx.r16=0x1234; ctx.r23=0x5678; ctx.r30=0x9abc;
        ctx.r4=display_list; ctx.r5=std::bit_cast<uint32_t>(1.25f);
        ctx.r6=std::bit_cast<uint32_t>(2.5f); ctx.r7=matrix;
        MEM_W(16,stack)=0;
        MEM_W(0,0xffffffff8076a0a8ULL)=map;
        MEM_W(0,0xffffffff807fc924ULL)=uint32_t(character);
        MEM_W(0,0xffffffff807fbb4cULL)=uint32_t(player);
        MEM_H(0x290,character)=chunk; MEM_H(0x12c,player)=view;
        MEM_H(0,0xffffffff807fd800ULL)=override;
        MEM_H(0x270,character)=10; MEM_H(0x272,character)=10;
        MEM_H(0x274,character)=309; MEM_H(0x276,character)=229;
        MEM_W(0,0xffffffff80754ce8ULL)=std::bit_cast<int32_t>(0.5f);
    }
    void run() {
        func_global_asm_80707980(rdram,&ctx);
        check(ctx.r29==stack && ctx.r31==0xffffffff80500000ULL && ctx.r16==0x1234
            && ctx.r23==0x5678 && ctx.r30==0x9abc,"skybox manager damaged caller state");
    }
};
}
extern "C" void switch_error(const char *,uint32_t,uint32_t) { check(false,"invalid skybox dispatch"); }
extern "C" void func_global_asm_80704B20(uint8_t *rdram,recomp_context *ctx) {
    ++skies;
    check(ctx->r4==display_list+8 && ctx->r7==matrix,"Galleon sky received the wrong display list/matrix");
    check(uint32_t(ctx->r5)==std::bit_cast<uint32_t>(1.25f) && uint32_t(ctx->r6)==std::bit_cast<uint32_t>(2.5f),"Galleon sky angles changed");
    check(MEM_BU(19,ctx->r29)==0 && MEM_BU(23,ctx->r29)==0 && MEM_BU(27,ctx->r29)==expected_tint
        && MEM_B(31,ctx->r29)==9 && MEM_W(32,ctx->r29)==0,"Galleon sky style/tint parameters changed");
    ctx->r2=ctx->r4+8;
}
extern "C" void func_global_asm_807069A4(uint8_t *rdram,recomp_context *ctx) {
    ++textured_skies;
    check(ctx->r7==0x2d && MEM_W(16,ctx->r29)==std::bit_cast<int32_t>(320.0f)
        && MEM_W(20,ctx->r29)==std::bit_cast<int32_t>(240.0f),"beetle-race sky texture parameters changed");
    ctx->r2=ctx->r4+8;
}
extern "C" void func_global_asm_8070770C(uint8_t *,recomp_context *ctx) { ++black_fills; ctx->r2=ctx->r4+8; }
extern "C" void func_global_asm_8061CB50(uint8_t *,recomp_context *ctx) { ctx->r2=0; }
extern "C" void sqrtf_recomp(uint8_t *,recomp_context *ctx) { ctx->f0.fl=std::sqrt(ctx->f12.fl); }
extern "C" void func_global_asm_8068B830(uint8_t *,recomp_context *) { check(false,"unexpected weather adjustment"); }
extern "C" void func_global_asm_8068B8A4(uint8_t *,recomp_context *) { check(false,"unexpected weather scaling"); }
extern "C" void func_global_asm_8068B8FC(uint8_t *,recomp_context *) { check(false,"unexpected weather reset"); }
extern "C" void func_global_asm_80705C00(uint8_t *,recomp_context *) { check(false,"unexpected fog change"); }
extern "C" void func_global_asm_80705F5C(uint8_t *,recomp_context *) { check(false,"unexpected fog geometry"); }
int main() {
    try {
        for(int chunk=0;chunk<=20;++chunk) for(int override:{0,1}) for(int view:{-1,0,3,9,11,12}) {
            Fixture f(30,chunk,view,override); auto *rdram=f.rdram; f.run();
            check(skies==1 && black_fills==0 && textured_skies==0,"Galleon chunk skipped its sky background");
            check(f.ctx.r2==display_list+24 && MEM_W(0,0xffffffff80754ce8ULL)==0,"Galleon sky did not finish its display list/reset blend");
        }
        {
            Fixture f(7,8,0,0); f.run();
            check(!skies && black_fills==1 && !textured_skies,"Japes interior background changed");
        }
        {
            Fixture f(14,0,0,0); f.run();
            check(!skies && !black_fills && textured_skies==1,"beetle-race textured sky changed");
        }
        {
            Fixture f(0,0,0,0); auto *rdram=f.rdram; f.run();
            // Fill-cycle endpoints are inclusive in this backend. Preserve the
            // original width/height minus one inside the existing scissor.
            check(uint32_t(MEM_W(32,display_list))==0xf64d0390 && uint32_t(MEM_W(36,display_list))==0x00028028,
                "unrelated background fill bounds changed");
            check(!skies && !black_fills && !textured_skies,"default background started a sky renderer");
        }
        std::puts("Skybox: generated Galleon chunk coverage, tint/view variants, caller state and other-map backgrounds passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
