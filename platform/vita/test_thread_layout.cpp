#include "ultramodern/ultra64.h"
#include <cstddef>
static_assert(offsetof(OSThread,next)==0);
static_assert(offsetof(OSThread,priority)==4);
static_assert(offsetof(OSThread,queue)==8);
static_assert(offsetof(OSThread,flags)==0x10);
static_assert(offsetof(OSThread,state)==0x12);
static_assert(offsetof(OSThread,id)==0x14);
static_assert(offsetof(OSThread,context)==0x20);
static_assert(offsetof(OSThread,sp)==0x28);
static_assert(sizeof(OSThread)==0x30);
int main() {}
