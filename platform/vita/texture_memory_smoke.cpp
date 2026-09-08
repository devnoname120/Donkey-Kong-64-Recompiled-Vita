#define RT64_TEXTURE_MEMORY_NO_MAIN
#include "fast/tests/rt64_fast_texture_memory_checks.cpp"
#include <psp2/io/stat.h>
#include <psp2/power.h>

int _newlib_heap_size_user=4*1024*1024;
unsigned int sceUserMainThreadStackSize=256*1024;

int main() {
    sceIoMkdir("ux0:data/rt64-texture-memory",0777);
    FILE *out=std::fopen("ux0:data/rt64-texture-memory/results.log","w");
    if(!out)return 1;
    std::setvbuf(out,nullptr,_IONBF,0);
    const int clockBefore=scePowerGetArmClockFrequency();
    std::fprintf(out,"cpu_mhz_before=%d\n",clockBefore);
    const int result=run_texture_memory_checks(out,out,true);
    const int clockAfter=scePowerGetArmClockFrequency();
    const int checked=result || clockBefore!=clockAfter;
    std::fprintf(out,"cpu_mhz_after=%d\nCOMPLETE exit=%d\n",clockAfter,checked);
    std::fclose(out);return checked;
}
