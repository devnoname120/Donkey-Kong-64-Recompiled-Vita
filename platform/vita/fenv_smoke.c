// Finite, in-range CVT.W.S/CVT.W.D cases under every guest rounding mode.
// Keep expected integers explicit: the oracle must not use the same libm or
// VFP conversion as the implementation being checked.
#include <stdint.h>
#include <stdio.h>
#include <fenv.h>
#include <math.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include "recomp.h"
int _newlib_heap_size_user=4*1024*1024;
unsigned int sceUserMainThreadStackSize=256*1024;
static uint32_t fpscr(void) { uint32_t v;__asm__ volatile("vmrs %0, fpscr":"=r"(v));return v; }
static __attribute__((noinline)) int32_t direct_s(float x) {
    union { float f;int32_t i; } r;
    __asm__ volatile("vcvtr.s32.f32 %0, %1":"=t"(r.f):"t"(x));return r.i;
}
static __attribute__((noinline)) int32_t direct_d(double x) {
    union { float f;int32_t i; } r;
    __asm__ volatile("vcvtr.s32.f64 %0, %P1":"=t"(r.f):"w"(x));return r.i;
}
struct Result { uint32_t mode,input,before,after;int read_mode,cs,cd,ds,dd,expected; };
int main(void) {
    static volatile float inputs[]={0.2f,-0.2f,0.5f,-0.5f,1.5f,2.5f,3.4f,-3.4f,3.8f,-3.8f,17.9f,-17.9f};
    static const int expected[4][12]={
        {0,0,0,0,2,2,3,-3,4,-4,18,-18},
        {0,0,0,0,1,2,3,-3,3,-3,17,-17},
        {1,0,1,0,2,3,4,-3,4,-3,18,-17},
        {0,-1,0,-1,1,2,3,-4,3,-4,17,-18}
    };
    static const uint32_t arm_modes[4]={0,3,1,2};
    struct Result results[48];unsigned n=0,failures=0;
    for(unsigned mode=0;mode<4;++mode) {
        set_cop1_cs(mode);
        for(unsigned i=0;i<12;++i) {
            union { float f;uint32_t u; } x={.f=inputs[i]};struct Result *r=&results[n++];
            r->mode=mode;r->input=x.u;r->read_mode=get_cop1_cs();r->before=fpscr();
            r->cs=do_cvt_w_s(x.f);r->cd=do_cvt_w_d((double)x.f);
            r->ds=direct_s(x.f);r->dd=direct_d((double)x.f);r->after=fpscr();
            r->expected=expected[mode][i];
            failures+=r->read_mode!=(int)mode || ((r->before>>22)&3)!=arm_modes[mode]
                || ((r->after>>22)&3)!=arm_modes[mode] || r->cs!=r->expected
                || r->cd!=r->expected || r->ds!=r->expected || r->dd!=r->expected;
        }
    }
    set_cop1_cs(0);sceIoMkdir("ux0:data/dk64-fenv",0777);
    FILE *out=fopen("ux0:data/dk64-fenv/results.csv","w");if(!out)return 1;
    fprintf(out,"mode,input_bits,reported,before_fpscr,after_fpscr,recomp_s,recomp_d,vcvtr_s,vcvtr_d,expected\n");
    for(unsigned i=0;i<n;++i) { const struct Result *r=&results[i];fprintf(out,"%u,%08x,%d,%08x,%08x,%d,%d,%d,%d,%d\n",r->mode,r->input,r->read_mode,r->before,r->after,r->cs,r->cd,r->ds,r->dd,r->expected); }
    const int result=fclose(out)!=0 || failures!=0;
    sceKernelExitProcess(result);return result;
}
