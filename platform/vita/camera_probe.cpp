#if DK64_VITA_PROBE_CAMERA
#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "log.h"
#include <array>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <vector>

extern "C" void setFlag(uint8_t *,recomp_context *);
extern "C" void isFlagSet(uint8_t *,recomp_context *);
extern "C" void changeCollectableCount(uint8_t *,recomp_context *);
namespace {
enum class Phase { Waiting,Settling,Entering,Miss,MissPhoto,PlaceFixture,Seeking,Locked,HitPhoto,Leaving,Finished };
struct CameraProbe {
    Phase phase=Phase::Waiting;
    bool prepared=false,enter_down=false,actors_reported=false;
    uint32_t next=0,press_until=0,last_picture=0;
    unsigned button=0,photos=0,queued=0,freed=0;
    std::array<uint32_t,2> buffers{};
    int previous_state=-1,previous_result=-1,previous_film=-1,previous_flag=-1;
    uint32_t next_report=0;
};
CameraProbe camera_probe;
bool valid(gpr address,uint32_t size) {
    const uint32_t value=uint32_t(address),offset=value&0x1fffffff;
    return value>=0x80000000 && value<0xc0000000 && offset<recomp::mem_size && size<=recomp::mem_size-offset;
}
recomp_context private_call(const recomp_context &ctx) {
    recomp_context call=ctx;call.r29=ADD32(call.r29,-0x20);
    // Bind aliases after returning this value; a copy must not retain pointers
    // into the temporary context's FPR storage.
    call.f_odd=nullptr;return call;
}
void bind_fprs(recomp_context &ctx) { ctx.f_odd=ctx.mips3_float_mode?&ctx.f1.u32l:&ctx.f0.u32h; }
int flag(uint8_t *rdram,const recomp_context &ctx,unsigned id) {
    auto call=private_call(ctx);bind_fprs(call);call.r4=id;call.r5=0;
    isFlagSet(rdram,&call);return int(call.r2);
}
void setup(uint8_t *rdram,const recomp_context &ctx) {
    auto call=private_call(ctx);bind_fprs(call);call.r4=0x179;call.r5=1;call.r6=0;
    setFlag(rdram,&call);
    call=private_call(ctx);bind_fprs(call);call.r4=6;call.r5=0;call.r6=10;
    changeCollectableCount(rdram,&call);
}
void prepare(CameraProbe &probe,uint8_t *rdram,const recomp_context &ctx,bool verify_save=false) {
    if(probe.prepared)return;
    if(verify_save) {
        const int captured=flag(rdram,ctx,0x24d),owned=flag(rdram,ctx,0x179);
        if(!captured || !owned)throw std::runtime_error("Camera probe saved capture is missing");
        probe.prepared=true;probe.phase=Phase::Finished;
        vita_log("Camera probe save verification passed: camera=%d fairy_flag=%d film=%u; no prerequisites changed",
            owned,captured,MEM_HU(0,0xffffffff807fcc48ULL));
        return;
    }
    if(flag(rdram,ctx,0x24d))throw std::runtime_error("Camera probe needs an uncaptured Japes pool fairy");
    setup(rdram,ctx);probe.prepared=true;
    vita_log("Camera probe prerequisites: camera=%d film=%u fairy_flag=%d",flag(rdram,ctx,0x179),
        MEM_HU(0,0xffffffff807fcc48ULL),flag(rdram,ctx,0x24d));
}
uint32_t packet(unsigned buttons=0,int x=0,int y=0) {
    return 0x10U | (x<0?1U:x>0?2U:0U) | ((y<0?1U:y>0?2U:0U)<<2) | (uint32_t(buttons)<<16);
}
void save_photo(uint8_t *rdram,uint32_t address,unsigned index) {
    std::vector<uint8_t> bytes(0xa000);
    for(unsigned i=0;i<bytes.size();++i)bytes[i]=rdram[((address&0x1fffffff)+i)^3];
    char path[112];std::snprintf(path,sizeof(path),"ux0:data/dk64recompiled-probe/camera-photo-%u.rgba16",index);
    FILE *out=std::fopen(path,"wb");
    if(!out)throw std::runtime_error("Could not create camera probe photograph");
    const size_t written=std::fwrite(bytes.data(),1,bytes.size(),out);std::fclose(out);
    if(written!=bytes.size())throw std::runtime_error("Incomplete camera probe photograph");
}
uint32_t update(CameraProbe &probe,uint8_t *rdram,recomp_context *ctx) {
    if(MEM_W(0,0xffffffff8076a0a8ULL)!=7) return packet();
    const gpr player=MEM_W(0,0xffffffff807fbb4cULL);
    if(!valid(player,0x17c))return packet();
    const gpr aad=MEM_W(0x174,player);
    if(!valid(aad,0x200))return packet();
    const uint32_t frame=MEM_W(0,0xffffffff8076a064ULL);
    if(probe.phase==Phase::Waiting) {
        if(MEM_BU(0,0xffffffff80755318ULL)!=6 || MEM_BU(0,0xffffffff80755314ULL)!=6
            || MEM_BU(0,0xffffffff807444ecULL) || MEM_BU(0,0xffffffff807463b8ULL)
            || (MEM_W(0,0xffffffff807fbb60ULL)&2) || (MEM_BU(0,0xffffffff8076a0b1ULL)&1)
            || std::bit_cast<float>(uint32_t(MEM_W(0,0xffffffff807fd88cULL)))!=0.0f)return packet();
        prepare(probe,rdram,*ctx);probe.phase=Phase::Settling;probe.next=frame+30;
    }
    uint32_t fairy=0,picture=0;
    const unsigned count=MEM_HU(0,0xffffffff807fbb34ULL);
    if(count>64)return packet();
    for(unsigned i=0;i<count;++i) {
        const gpr actor=MEM_W(i*8,0xffffffff807fb930ULL);
        if(!valid(actor,0x17c))continue;
        const int type=MEM_W(0x58,actor);
        if(!probe.actors_reported)vita_log("Camera probe actor: ptr=%08x type=%d pos=%.1f,%.1f,%.1f",uint32_t(actor),type,
            std::bit_cast<float>(uint32_t(MEM_W(0x7c,actor))),std::bit_cast<float>(uint32_t(MEM_W(0x80,actor))),std::bit_cast<float>(uint32_t(MEM_W(0x84,actor))));
        if(type==248)fairy=uint32_t(actor);
        if(type==202 && (MEM_W(0x60,actor)&0x10))picture=uint32_t(actor);
    }
    if(count)probe.actors_reported=true;
    if(picture && picture!=probe.last_picture && probe.photos<probe.buffers.size()) {
        const gpr photo_aad=MEM_W(0x174,int32_t(picture));
        if(valid(photo_aad,0x14)) {
            const uint32_t pixels=MEM_W(4,photo_aad);
            if(valid(int32_t(pixels),0xa000)) {
                save_photo(rdram,pixels,probe.photos);probe.buffers[probe.photos++]=pixels;
                vita_log("Camera probe picture: index=%u actor=%08x pixels=%08x result=%u",probe.photos-1,picture,pixels,MEM_BU(0x1ec,aad));
            }
        }
    }
    if(probe.last_picture && !picture)vita_log("Camera probe picture removed: actor=%08x queued=%u freed=%u",probe.last_picture,probe.queued,probe.freed);
    probe.last_picture=picture;
    const int state=MEM_BU(0x154,player),result=MEM_BU(0x1ec,aad),film=MEM_HU(0,0xffffffff807fcc48ULL);
    const bool busy=MEM_W(0x1f0,aad)&0x8000,active=state==4||state==5;
    const int captured=flag(rdram,*ctx,0x24d);
    const int sx=MEM_H(0,0xffffffff807fd804ULL),sy=MEM_H(0,0xffffffff807fd806ULL);
    if(state!=probe.previous_state || result!=probe.previous_result || film!=probe.previous_film
        || captured!=probe.previous_flag || int32_t(frame-probe.next_report)>=0) {
        vita_log("Camera probe frame=%u phase=%u state=%d result=%d film=%d busy=%u fairy=%08x aim=%d,%d range=%d captured=%d picture=%08x queued=%u freed=%u pos=%.1f,%.1f,%.1f fairy_pos=%.1f,%.1f,%.1f",
            frame,unsigned(probe.phase),state,result,film,unsigned(busy),fairy,sx,sy,MEM_H(0,0xffffffff807fd802ULL),captured,picture,probe.queued,probe.freed,
            std::bit_cast<float>(uint32_t(MEM_W(0x7c,player))),std::bit_cast<float>(uint32_t(MEM_W(0x80,player))),std::bit_cast<float>(uint32_t(MEM_W(0x84,player))),
            fairy?std::bit_cast<float>(uint32_t(MEM_W(0x7c,int32_t(fairy)))):0.0f,fairy?std::bit_cast<float>(uint32_t(MEM_W(0x80,int32_t(fairy)))):0.0f,fairy?std::bit_cast<float>(uint32_t(MEM_W(0x84,int32_t(fairy)))):0.0f);
        probe.next_report=frame+30;
    }
    probe.previous_state=state;probe.previous_result=result;probe.previous_film=film;probe.previous_flag=captured;
    if(int32_t(frame-probe.press_until)<0)return packet(probe.button);
    auto press=[&](unsigned button,Phase next) { probe.button=button;probe.press_until=frame+2;probe.phase=next;return packet(button); };
    switch(probe.phase) {
    case Phase::Waiting:break;
    case Phase::Settling:
        if(int32_t(frame-probe.next)>=0) {
            probe.enter_down=false;probe.next=frame+8;
            const uint32_t input=press(0x2000,Phase::Entering);probe.press_until=frame+8;return input;
        }
        break;
    case Phase::Entering:
        if(active) { probe.phase=probe.photos?Phase::Seeking:Phase::Miss;probe.next=frame+20;vita_log("Camera probe entered camera"); }
        else if(int32_t(frame-probe.next)>=0) {
            if(!probe.enter_down) { probe.enter_down=true;probe.next=frame+60;return press(0x2004,Phase::Entering); }
            probe.phase=Phase::Settling;probe.next=frame+15;
        }
        break;
    case Phase::Miss:
        if(active && !busy && int32_t(frame-probe.next)>=0) {
            if(result==1)return packet(0,1,0);
            vita_log("Camera probe miss shutter");return press(0x4000,Phase::MissPhoto);
        }
        break;
    case Phase::MissPhoto:
        if(probe.photos>=1 && !picture && !busy && (probe.freed&1)) {
            if(captured)throw std::runtime_error("Camera probe's intended miss captured the fairy");
            return press(4,Phase::PlaceFixture);
        }
        break;
    case Phase::PlaceFixture:
        if(!active && state!=0x64 && state!=0x65 && !busy && !MEM_W(0x88,aad)) {
            MEM_W(0x7c,player)=std::bit_cast<uint32_t>(700.0f);
            MEM_W(0x80,player)=std::bit_cast<uint32_t>(320.0f);
            MEM_W(0x84,player)=std::bit_cast<uint32_t>(3080.0f);
            probe.phase=Phase::Settling;probe.next=frame+120;
            vita_log("Camera probe fixture: player placed at 700,320,3080; original physics, visibility and capture remain active");
        }
        break;
    case Phase::Seeking:
        if(active && !busy && int32_t(frame-probe.next)>=0) {
            if(fairy && result==1 && sx>=154 && sx<=166 && sy>=112 && sy<=124) {
                probe.phase=Phase::Locked;probe.next=frame+4;
                return packet();
            }
            if(fairy && sx!=16384 && sy!=16384) {
                // Coarse aiming overshoots while the game's filtered input settles.
                const uint32_t precision=(sx>=80 && sx<=240 && sy>=40 && sy<=200)?0x20U:0;
                return packet(0,sx<154?-1:sx>166?1:0,sy<112?-1:sy>124?1:0)|precision;
            }
            return packet(0,1,0);
        }
        break;
    case Phase::Locked:
        if(!active || busy || !fairy || result!=1 || sx<144 || sx>172 || sy<102 || sy>130) {
            probe.phase=Phase::Seeking;probe.next=frame;
        } else if(int32_t(frame-probe.next)>=0) {
            vita_log("Camera probe fairy shutter: settled projection=%d,%d",sx,sy);
            return press(0x4000,Phase::HitPhoto);
        }
        break;
    case Phase::HitPhoto:
        if(probe.photos>=2 && !picture && !busy && (probe.freed&2)) {
            if(!captured)throw std::runtime_error("Camera probe fairy recognition did not persist");
            probe.next=frame+60;return press(4,Phase::Leaving);
        }
        break;
    case Phase::Leaving:
        if((state==12 || state==13) && !busy && !MEM_W(0x88,aad)) {
            probe.phase=Phase::Finished;probe.next=frame+30;
            vita_log("Camera probe completed: photos=%u film=%d fairy_flag=%d queued=%u freed=%u",probe.photos,film,captured,probe.queued,probe.freed);
        }
        break;
    case Phase::Finished:
        if(int32_t(frame-probe.next)<0)return packet(0,1,0);
        break;
    }
    return packet();
}
int buffer_index(uint32_t address,unsigned mask) {
    for(unsigned i=0;i<camera_probe.photos;++i)if(camera_probe.buffers[i]==address && !(mask&(1U<<i)))return int(i);
    return -1;
}
}
extern "C" uint32_t dk64_vita_camera_probe(uint8_t *rdram,recomp_context *ctx) { return update(camera_probe,rdram,ctx); }
extern "C" void dk64_vita_prepare_camera_probe(uint8_t *rdram,recomp_context *ctx) {
    if(camera_probe.prepared)return;
    FILE *verification=std::fopen("ux0:data/dk64recompiled-probe/verify-camera-save","rb");
    const bool verify_save=verification!=nullptr;
    if(verification)std::fclose(verification);
    prepare(camera_probe,rdram,*ctx,verify_save);
}
extern "C" void __real_func_global_asm_8061134C(uint8_t *,recomp_context *);
extern "C" void __wrap_func_global_asm_8061134C(uint8_t *rdram,recomp_context *ctx) {
    const uint32_t address=ctx->r4;const int index=buffer_index(address,camera_probe.queued);
    __real_func_global_asm_8061134C(rdram,ctx);
    if(index>=0) { camera_probe.queued|=1U<<index;vita_log("Camera probe photo free requested: index=%d pixels=%08x",index,address); }
}
extern "C" void __real_func_global_asm_80611408(uint8_t *,recomp_context *);
extern "C" void __wrap_func_global_asm_80611408(uint8_t *rdram,recomp_context *ctx) {
    const uint32_t header=ctx->r4;const int index=buffer_index(header+0x10,camera_probe.freed);
    __real_func_global_asm_80611408(rdram,ctx);
    if(index>=0) { camera_probe.freed|=1U<<index;vita_log("Camera probe photo free completed: index=%d header=%08x",index,header); }
}
#endif
