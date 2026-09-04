#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned long wia_numsetbits(const RTL_BITMAP*);
unsigned long ref_numsetbits(const RTL_BITMAP*);
typedef ULONG (WINAPI *fn)(const RTL_BITMAP*);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlNumberOfSetBits");
    static unsigned long buf[2100]; unsigned long seed=1;
    for(int nbits=0; nbits<=4096 && failures<8; ++nbits){
        int nwords=(nbits+31)/32;
        for(int i=0;i<nwords+2;i++){ seed=seed*1103515245u+12345u; buf[i]=seed; }
        RTL_BITMAP bm={(unsigned long)nbits, buf};
        unsigned long y=sys(&bm), o=wia_numsetbits(&bm), r=ref_numsetbits(&bm);
        if(y!=o || o!=r){ printf("FAIL nbits=%d: ntdll=%lu ours=%lu ref=%lu\n",nbits,y,o,r); ++failures; }
    }
    if(!failures) printf("CORRECTNESS: PASS (popcount nbits=0..4096, random incl. nonzero trailing, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
