#include "recomp.h"
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void func_global_asm_8070A934(uint8_t *,recomp_context *);
extern "C" void func_global_asm_8070B05C(uint8_t *,recomp_context *);
namespace {
constexpr gpr index=0xffffffff807444fcULL,phase=0xffffffff807444f8ULL;
constexpr gpr initialized=0xffffffff807fd9bcULL,progress=0xffffffff80755068ULL;
constexpr gpr framebuffer=0xffffffff80500000ULL,vertices=0xffffffff80460000ULL;
constexpr gpr model=0xffffffff80470000ULL,geometry=0xffffffff80480000ULL;
constexpr gpr tiles=0xffffffff80490000ULL,original_vertices=0xffffffff804a0000ULL;
constexpr gpr matrix0=0xffffffff804b0000ULL,matrix1=0xffffffff804c0000ULL;
constexpr gpr stack=0xffffffff806f0000ULL;
unsigned allocations=0,syncs=0,renders=0,loads=0,blacks=0,releases=0,restores=0,submits=0,secondary_submits=0;
bool complete=false;
std::vector<gpr> freed;
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
uint16_t pixel(unsigned n) { return uint16_t((n*174+0x1246)&0xfffe); }
gpr source(unsigned i) { return 0xffffffff80300000ULL+i*0x40000; }
gpr displayList(unsigned i) { return 0xffffffff80400000ULL+i*0x10000; }
void setFloat(uint8_t *rdram,gpr address,float value) { MEM_W(0,address)=std::bit_cast<int32_t>(value); }
struct Fixture {
    std::vector<uint8_t> memory=std::vector<uint8_t>(8*1024*1024);
    uint8_t *rdram=memory.data();
    recomp_context ctx{};
    explicit Fixture(unsigned transition_phase) {
        allocations=syncs=renders=loads=blacks=releases=restores=submits=secondary_submits=0;
        complete=false; freed.clear(); ctx.r29=stack; ctx.r31=0xffffffff80600000ULL;
        ctx.r16=16; ctx.r23=23; ctx.r30=30;
        MEM_B(0,phase)=transition_phase; MEM_B(0,0xffffffff807444f4ULL)=1;
        MEM_B(0,0xffffffff807444ecULL)=6;
        MEM_H(0,0xffffffff80744490ULL)=320; MEM_H(0,0xffffffff80744494ULL)=240;
        MEM_W(0,0xffffffff8076a088ULL)=0x1234;
        MEM_W(0,0xffffffff8076a064ULL)=50;
        MEM_B(0,0xffffffff807501e0ULL)=2; // Existing non-snapshot overlays.
        for(unsigned i=0;i<2;++i) {
            MEM_W(i*4,0xffffffff80744470ULL)=uint32_t(source(i));
            MEM_W(i*4,0xffffffff8076a050ULL)=uint32_t(displayList(i));
        }
        // The original mask table at 0x80755074 clears only RGBA16 alpha.
        // Four six-pixel rows have cleared bits at columns {1,4}, {3,4,5},
        // {1,4}, {0,1,2}. Its decompressed-ROM location is 0x02159d74.
        constexpr unsigned masks[]={0x12,0x38,0x12,0x07};
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<6;++x)
            MEM_H((y*6+x)*2,0xffffffff80755074ULL)=0xffff^((masks[y]>>x)&1);
    }
    void step() {
        ctx.r4=31; ctx.r5=4;
        func_global_asm_8070A934(rdram,&ctx);
        check(ctx.r29==stack && ctx.r31==0xffffffff80600000ULL && ctx.r16==16 && ctx.r23==23 && ctx.r30==30,
            "zipper driver damaged its caller state");
        check(MEM_BU(0,0xffffffff807501e0ULL)==2,"CPU zipper cleared unrelated overlay registrations");
    }
};
}
extern "C" void _malloc(uint8_t *,recomp_context *ctx) {
    check(allocations<2,"zipper repeated its snapshot/vertex allocation");
    check(ctx->r4==(allocations?4*12:320*240*2),"zipper allocation size changed");
    ctx->r2=allocations++?vertices:framebuffer;
}
extern "C" void getPointerTableFile(uint8_t *,recomp_context *ctx) {
    check(ctx->r4==19 && ctx->r5==1 && ctx->r6==1 && ctx->r7==1,"zipper loaded the wrong model");
    ctx->r2=model;
}
extern "C" void dk64_vita_sync_framebuffer(uint8_t *rdram,uint32_t address,uint32_t width,uint32_t height) {
    check(address==uint32_t(source(MEM_BU(0,index))) && width==320 && height==240,"zipper synchronized the wrong source framebuffer");
    ++syncs;
    for(unsigned p=0;p<320*240;++p) MEM_H(p*2,address)=pixel(p);
}
extern "C" void func_global_asm_80709890(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==model && ctx->r7==0,"zipper parsed the wrong model");
    MEM_W(0,ctx->r5)=uint32_t(geometry); MEM_W(0,ctx->r6)=0;
    MEM_W(0,geometry)=4; MEM_W(8,geometry)=uint32_t(original_vertices);
    MEM_W(0x10,geometry)=uint32_t(matrix0); MEM_W(0x14,geometry)=uint32_t(matrix1);
    MEM_W(0x24,geometry)=32; MEM_W(0x28,geometry)=16;
}
extern "C" void func_global_asm_80709ACC(uint8_t *,recomp_context *ctx) { check(ctx->r4==geometry,"zipper grouped the wrong geometry"); }
extern "C" void func_global_asm_807095E4(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==32 && ctx->r5==16,"zipper lost the model's texture tile dimensions");
    MEM_W(0,0xffffffff807fd9b4ULL)=uint32_t(tiles);
}
extern "C" void func_global_asm_80610044(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==displayList(MEM_BU(0,index)) && ctx->r5==0x1234 && ctx->r6==3 && ctx->r7==1
        && MEM_W(0x10,ctx->r29)==0x4d2 && MEM_W(0x14,ctx->r29)==1,"zipper submitted the wrong previous frame");
    ++submits;
}
extern "C" void func_global_asm_8070AC74(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==0xffffffff80767ce8ULL+MEM_BU(0,index)*0x11b0,"zipper selected the wrong matrix bank");
    MEM_W(0,ctx->r5)=uint32_t(displayList(MEM_BU(0,index)));
}
extern "C" void func_global_asm_8070B7EC(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r5==model && ctx->r6==framebuffer,"zipper renderer stopped receiving its owned CPU snapshot");
    MEM_W(0,ctx->r4)+=8; ++renders; ctx->r2=complete;
}
extern "C" void osViBlack_recomp(uint8_t *,recomp_context *ctx) { check(ctx->r4==1,"zipper unblanked the VI during loading"); ++blacks; }
extern "C" void func_global_asm_805FF378(uint8_t *,recomp_context *ctx) { check(ctx->r4==31 && ctx->r5==4,"zipper changed its destination map/exit"); ++loads; }
extern "C" void func_global_asm_8061134C(uint8_t *,recomp_context *ctx) { freed.push_back(ctx->r4); }
extern "C" void func_global_asm_8066B434(uint8_t *,recomp_context *ctx) { check(ctx->r4==model && ctx->r5==0x24b && ctx->r6==0x4a,"zipper changed model-release ownership"); ++releases; }
extern "C" void func_global_asm_8061CBCC(uint8_t *,recomp_context *) { ++restores; }
extern "C" void func_global_asm_805FE71C(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r5==MEM_BU(0,index) && ctx->r6==0xffffffff8076a088ULL,"zipper finalized the wrong frame");
    check(ctx->r4==displayList(MEM_BU(0,index))+(renders?8:0),"zipper lost its display-list cursor");
}
extern "C" void func_global_asm_805FE7B4(uint8_t *rdram,recomp_context *ctx) {
    const gpr bank=0xffffffff80767ce8ULL+MEM_BU(0,index)*0x11b0;
    check(ctx->r4==bank+0xdb0 && ctx->r5==bank && ctx->r6==0xffffffff8076a08cULL && ctx->r7==1,"zipper changed secondary-frame submission");
    ++secondary_submits;
}
int main() {
    try {
        {
            Fixture f(1); auto *rdram=f.rdram;
            f.step();
            check(allocations==2 && syncs==1 && renders==1 && MEM_W(0,progress)==0,"zipper did not initialize once and render in the same call");
            check(MEM_BU(0,index)==1 && MEM_W(0,0xffffffff8076a064ULL)==51,"zipper did not advance its frame bank/timer");
            check(uint32_t(MEM_W(0,0xffffffff807fd9a4ULL))==uint32_t(original_vertices)
                && uint32_t(MEM_W(8,geometry))==uint32_t(vertices),"zipper lost the original or mutable vertex array");
            for(unsigned p=0;p<320*240;++p)
                check(MEM_HU(p*2,framebuffer)==(pixel(p)|1),"zipper copied stale pixels or failed to make its snapshot opaque");
            f.step();
            check(syncs==1 && allocations==2 && renders==2,"zipper recaptured or reallocated on its second frame");
            complete=true; f.step();
            check(blacks==1 && loads==1 && MEM_BU(0,phase)==2 && MEM_W(0,0xffffffff807fd888ULL)==std::bit_cast<int32_t>(31.0f)
                && freed.empty(),"closing zipper did not hand off to the requested map");
        }
        {
            Fixture f(2); auto *rdram=f.rdram;
            MEM_B(0,0xffffffff807fd9bdULL)=2; MEM_B(0,0xffffffff8076a0a4ULL)=1;
            MEM_W(0,0xffffffff807fbb64ULL)=1;
            f.step();
            check(MEM_W(0,progress)==120 && MEM_BU(0,0xffffffff807fd9bdULL)==1 && secondary_submits==0,"opening zipper initialization/countdown changed");
            complete=true; f.step();
            check(freed==std::vector<gpr>{framebuffer,vertices,tiles,matrix0,matrix1} && releases==1,"opening zipper did not free each owned buffer once");
            check(MEM_BU(0,phase)==3 && MEM_W(0,0xffffffff807fd888ULL)==0 && restores==1 && secondary_submits==1,
                "opening zipper did not restore its prior mode/submission path");
            check(MEM_BU(0,0xffffffff807fd9bdULL)==0 && (MEM_BU(0,0xffffffff8076a0b1ULL)&2),"opening zipper countdown did not signal completion");
        }
        {
            Fixture f(1); auto *rdram=f.rdram;
            MEM_B(0,initialized)=1; MEM_B(0,0xffffffff8076a0b1ULL)=1;
            MEM_B(0,0xffffffff8076a0b2ULL)=1; setFloat(rdram,0xffffffff807fd888ULL,31.0f);
            f.step();
            check(!allocations && !syncs && !renders && MEM_BU(0,0xffffffff807444ecULL)==1,"completed-frame reuse unexpectedly rebuilt the zipper");
        }
        for(int tick:{5,6,7,8,9,44,84,85}) for(unsigned seed:{0u,1u}) {
            Fixture f(1); auto *rdram=f.rdram;
            for(unsigned p=0;p<320*240;++p) MEM_H(p*2,framebuffer)=pixel(p)|((p+seed)&1);
            MEM_H(-2,framebuffer)=0x2468; MEM_H(320*240*2,framebuffer)=0x1357;
            MEM_W(0,progress)=tick; f.ctx.r4=framebuffer;
            func_global_asm_8070B05C(rdram,&f.ctx);
            constexpr unsigned clear_bits[]={0x12,0x38,0x12,0x07};
            const unsigned masked_rows=(tick-5)*3;
            for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
                unsigned expected=pixel(y*320+x)|((y*320+x+seed)&1);
                if(x>=157 && x<163) {
                    // Above the zipper head, AND preserves an already clear
                    // alpha bit; below it, OR restores the six center pixels.
                    if(y>=masked_rows) expected|=1;
                    else expected&=0xffff^((clear_bits[y%4]>>(x-157))&1);
                }
                check(MEM_HU((y*320+x)*2,framebuffer)==expected,"zipper morpher changed RGB, neighboring pixels or its alpha seam");
            }
            check(MEM_HU(-2,framebuffer)==0x2468 && MEM_HU(320*240*2,framebuffer)==0x1357,"zipper morpher wrote beyond its snapshot");
            check(f.ctx.r29==stack,"zipper morpher damaged its stack");
        }
        std::puts("Zipper: generated snapshot ownership, single initialization, handoff, releases, frame reuse and CPU alpha seam passed");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
