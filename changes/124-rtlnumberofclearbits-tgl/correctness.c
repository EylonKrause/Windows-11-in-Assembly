// changes/124-rtlnumberofclearbits/correctness.c
// Bit-exact fuzz of wia_numclearbits vs live ntdll!RtlNumberOfClearBits + oracle, nbits 0..4096
// (incl. nonzero trailing bits beyond SizeOfBitMap to verify masking).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned long wia_numclearbits(const RTL_BITMAP*);
unsigned long ref_numclearbits(const RTL_BITMAP*);
typedef ULONG (WINAPI *fn)(const RTL_BITMAP*);
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlNumberOfClearBits");
    if(!sys){ printf("no RtlNumberOfClearBits\n"); return 2; }
    static unsigned long buf[2100]; unsigned long seed=1; int fails=0;
    for(int nbits=0; nbits<=4096 && fails<8; ++nbits){
        int nwords=(nbits+31)/32;
        for(int i=0;i<nwords+2;i++){ seed=seed*1103515245u+12345u; buf[i]=seed; }
        RTL_BITMAP bm={(unsigned long)nbits, buf};
        unsigned long y=sys(&bm), o=wia_numclearbits(&bm), r=ref_numclearbits(&bm);
        if(y!=o || o!=r){ printf("FAIL nbits=%d: ntdll=%lu ours=%lu ref=%lu\n",nbits,y,o,r); ++fails; }
    }
    if(!fails) printf("CORRECTNESS: PASS (clear-bit count nbits=0..4096, random incl. nonzero trailing, vs live + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
