#define DK64_VITA_PROBE_CAMERA 1
#include "camera_probe.cpp"
#include <cstring>

namespace {
constexpr gpr stack=0xffffffff80600000ULL,player=0xffffffff80401000ULL,paad=0xffffffff80402000ULL;
unsigned grants=0,refills=0,release_requests=0,releases=0;
bool camera_owned=false,fairy_captured=false;
void check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
void spill(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r29==stack-0x20,"camera setup has no private argument area");
    check(ctx->f_odd==(ctx->mips3_float_mode?&ctx->f1.u32l:&ctx->f0.u32h),"camera setup retained another context's FPR aliases");
    MEM_W(0,ctx->r29)=0x12345678;ctx->r16=0xabcdef;ctx->f_odd[0]=0x87654321;
}
void ready(uint8_t *rdram) {
    MEM_W(0,0xffffffff8076a0a8ULL)=7;MEM_W(0,0xffffffff807fbb4cULL)=int32_t(player);MEM_W(0x174,player)=int32_t(paad);
    MEM_B(0,0xffffffff80755318ULL)=6;MEM_B(0,0xffffffff80755314ULL)=6;
    MEM_B(0,0xffffffff807444ecULL)=0;MEM_B(0,0xffffffff807463b8ULL)=0;
    MEM_W(0,0xffffffff807fbb60ULL)=0;MEM_B(0,0xffffffff8076a0b1ULL)=0;MEM_W(0,0xffffffff807fd88cULL)=0;
    MEM_H(0,0xffffffff807fbb34ULL)=0;MEM_W(0,0xffffffff8076a064ULL)=100;
    MEM_B(0x154,player)=12;MEM_B(0x1ec,paad)=0;MEM_W(0x1f0,paad)=0;
    MEM_W(0x7c,player)=std::bit_cast<uint32_t>(650.0f);MEM_W(0x84,player)=std::bit_cast<uint32_t>(3120.0f);
}
}
void vita_log(const char *,...) {}
extern "C" void setFlag(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==0x179 && ctx->r5==1 && ctx->r6==0,"probe changed a flag other than camera ownership");
    spill(rdram,ctx);camera_owned=true;++grants;
}
extern "C" void isFlagSet(uint8_t *rdram,recomp_context *ctx) {
    const auto id=ctx->r4;check((id==0x179||id==0x24d)&&ctx->r5==0,"unexpected camera flag query");
    spill(rdram,ctx);ctx->r2=id==0x179?camera_owned:fairy_captured;
}
extern "C" void changeCollectableCount(uint8_t *rdram,recomp_context *ctx) {
    check(ctx->r4==6 && ctx->r5==0 && ctx->r6==10,"probe refilled something other than film");
    spill(rdram,ctx);MEM_H(0,0xffffffff807fcc48ULL)=10;++refills;
}
extern "C" void __real_func_global_asm_8061134C(uint8_t *,recomp_context *ctx) { ++release_requests;ctx->r4=SUB32(ctx->r4,0x10); }
extern "C" void __real_func_global_asm_80611408(uint8_t *,recomp_context *ctx) { ++releases;ctx->r2=0x55; }
int main() {
    try {
        std::vector<uint8_t> memory(recomp::mem_size);auto *rdram=memory.data();
        for(unsigned mode:{0U,1U}) {
            ready(rdram);grants=refills=0;camera_owned=false;
            recomp_context ctx{};ctx.r29=stack;ctx.mips3_float_mode=mode;bind_fprs(ctx);const auto before=ctx;
            MEM_W(0,stack)=0x11223344;
            CameraProbe probe;MEM_B(0,0xffffffff807444ecULL)=1;
            check(update(probe,rdram,&ctx)==packet()&&!grants&&!refills,"camera setup bypassed active cutscene");
            MEM_B(0,0xffffffff807444ecULL)=0;
            check(update(probe,rdram,&ctx)==packet()&&grants==1&&refills==1,"camera setup failed to provision only its prerequisites");
            check(std::memcmp(&before,&ctx,sizeof(ctx))==0&&MEM_W(0,stack)==0x11223344,"camera setup modified caller state");
            update(probe,rdram,&ctx);check(grants==1&&refills==1,"camera setup repeated its mutations");
            MEM_W(0,0xffffffff8076a064ULL)=130;
            check(update(probe,rdram,&ctx)==packet(0x2000),"camera entry did not hold Z first");
            MEM_W(0,0xffffffff8076a064ULL)=138;
            check(update(probe,rdram,&ctx)==packet(0x2004),"camera entry omitted the later C-Down edge");
            MEM_W(0,0xffffffff8076a064ULL)=140;MEM_B(0x154,player)=4;
            update(probe,rdram,&ctx);check(probe.phase==Phase::Miss,"camera entry was not observed");
            MEM_W(0,0xffffffff8076a064ULL)=160;MEM_B(0x1ec,paad)=2;
            check(update(probe,rdram,&ctx)==packet(0x4000)&&probe.phase==Phase::MissPhoto,"camera shutter is not normal B input");
        }
        ready(rdram);recomp_context navigation{};navigation.r29=stack;bind_fprs(navigation);
        ready(rdram);MEM_B(0x154,player)=4;
        constexpr gpr fairy=0xffffffff80404000ULL;
        MEM_H(0,0xffffffff807fbb34ULL)=1;MEM_W(0,0xffffffff807fb930ULL)=int32_t(fairy);
        MEM_W(0x58,fairy)=248;MEM_H(0,0xffffffff807fd804ULL)=155;
        CameraProbe aiming;aiming.prepared=true;aiming.phase=Phase::Seeking;aiming.photos=1;
        MEM_H(0,0xffffffff807fd806ULL)=-40;
        check(update(aiming,rdram,&navigation)==packet(0,0,-1),"camera probe did not pull the stick back to aim upward");
        MEM_H(0,0xffffffff807fd806ULL)=280;
        check(update(aiming,rdram,&navigation)==packet(0,0,1),"camera probe did not push the stick forward to aim downward");
        MEM_H(0,0xffffffff807fd804ULL)=16384;MEM_H(0,0xffffffff807fd806ULL)=16384;
        check(update(aiming,rdram,&navigation)==packet(0,1,0),"camera probe aimed at the invalid projection sentinel");
        MEM_B(0x1ec,paad)=1;MEM_H(0,0xffffffff807fd804ULL)=100;MEM_H(0,0xffffffff807fd806ULL)=94;
        check(update(aiming,rdram,&navigation)==(packet(0,-1,-1)|0x20),
            "camera probe used coarse input near the target or trusted a previous projection");
        MEM_H(0,0xffffffff807fd804ULL)=153;MEM_H(0,0xffffffff807fd806ULL)=120;
        check(update(aiming,rdram,&navigation)==(packet(0,-1,0)|0x20),
            "camera steering stopped outside the shutter locking window");
        MEM_H(0,0xffffffff807fd804ULL)=160;MEM_H(0,0xffffffff807fd806ULL)=120;
        check(update(aiming,rdram,&navigation)==packet() && aiming.phase!=Phase::HitPhoto,
            "camera probe fired before the neutral stick reached the game");
        MEM_W(0,0xffffffff8076a064ULL)=103;
        check(update(aiming,rdram,&navigation)==packet(),"camera probe did not settle before the shutter");
        MEM_W(0,0xffffffff8076a064ULL)=104;
        check(update(aiming,rdram,&navigation)==packet(0x4000) && aiming.phase==Phase::HitPhoto,
            "camera probe did not fire at a settled recognized fairy");
        ready(rdram);
        CameraProbe placement;placement.prepared=true;placement.phase=Phase::PlaceFixture;placement.photos=1;
        const auto before_placement=navigation;
        check(update(placement,rdram,&navigation)==packet(),"camera fixture injected a shutter while repositioning");
        check(placement.phase==Phase::Settling && placement.next==220,"camera fixture did not allow physics and camera settling");
        check(MEM_W(0x7c,player)==std::bit_cast<uint32_t>(700.0f)
            && MEM_W(0x80,player)==std::bit_cast<uint32_t>(320.0f)
            && MEM_W(0x84,player)==std::bit_cast<uint32_t>(3080.0f),"camera fixture was not placed beside the pool");
        check(MEM_B(0x1ec,paad)==0 && std::memcmp(&before_placement,&navigation,sizeof(navigation))==0,
            "camera fixture changed recognition or the caller context");
        for(unsigned mode:{0U,1U}) {
            ready(rdram);camera_owned=true;fairy_captured=true;grants=refills=0;
            recomp_context saved{};saved.r29=stack;saved.mips3_float_mode=mode;bind_fprs(saved);
            const auto before=saved;
            CameraProbe verification;
            prepare(verification,rdram,saved,true);
            check(verification.prepared&&verification.phase==Phase::Finished&&!grants&&!refills,
                "save verification modified progress or started another photograph");
            check(std::memcmp(&before,&saved,sizeof(saved))==0,"save verification changed caller state");
            fairy_captured=false;bool rejected=false;
            try { CameraProbe missing;prepare(missing,rdram,saved,true); }
            catch(const std::runtime_error &) { rejected=true; }
            check(rejected&&!grants&&!refills,"save verification created missing progress");
            fairy_captured=true;rejected=false;
            try { CameraProbe normal;prepare(normal,rdram,saved); }
            catch(const std::runtime_error &) { rejected=true; }
            check(rejected,"capture test accepted a previously captured fairy");
        }
        fairy_captured=false;
        ready(rdram);CameraProbe exiting;exiting.prepared=true;exiting.phase=Phase::Leaving;
        MEM_B(0x154,player)=0x83;
        update(exiting,rdram,&navigation);
        check(exiting.phase==Phase::Leaving,"camera test declared completion during the fairy reward animation");
        MEM_B(0x154,player)=12;update(exiting,rdram,&navigation);
        check(exiting.phase==Phase::Finished,"camera test did not observe normal control after the reward");
        camera_probe={};camera_probe.photos=2;camera_probe.buffers={0x80500000,0x80510000};
        recomp_context ctx{};ctx.r4=int32_t(0x80500000);
        __wrap_func_global_asm_8061134C(rdram,&ctx);
        check(camera_probe.queued==1&&release_requests==1&&uint32_t(ctx.r4)==0x804ffff0,"photo release observer altered the real call");
        __wrap_func_global_asm_80611408(rdram,&ctx);
        check(camera_probe.freed==1&&releases==1&&ctx.r2==0x55,"photo release observer lost header/payload correspondence");
        std::puts("Camera probe: prerequisite scope, one-time setup, caller/FPR preservation, controller entry/shutter and release forwarding passed");
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
