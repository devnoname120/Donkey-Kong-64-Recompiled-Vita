#include <psp2/io/stat.h>
#include <cstdio>
#define RT64_CPU_CONTROL
#include "fast/tests/rt64_fast_rsp_checks.cpp"

int _newlib_heap_size_user=64*1024*1024;
unsigned int sceUserMainThreadStackSize=2*1024*1024;

int main() {
    sceIoMkdir("ux0:data/rt64-cpu",0777);
    FILE *output=std::fopen("ux0:data/rt64-cpu/results.log","w");
    if(!output)return 1;
    std::setvbuf(output,nullptr,_IONBF,0);
    const int result=run_rsp_checks(true,output,output);
    std::fprintf(output,"%s: RSP CPU control\n",result?"FAIL":"PASS");
    std::fclose(output);
    return result;
}
