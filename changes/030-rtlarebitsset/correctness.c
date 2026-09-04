#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned char wia_arebitsset(const RTL_BITMAP*, unsigned long, unsigned long);
unsigned char ref_arebitsset(const RTL_BITMAP*, unsigned long, unsigned long);
typedef BOOLEAN (WINAPI *fn)(const RTL_BITMAP*, ULONG, ULONG);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlAreBitsSet");
    static unsigned long buf[300]; unsigned long seed=1;
    int size=8000;
    for(int t=0;t<200000 && failures<10;t++){
        // vary the buffer sometimes (mostly all-ones, occasional holes)
        if(t%1000==0){ for(int i=0;i<300;i++) buf[i]=0xFFFFFFFF;
            for(int k=0;k<t/1000 && k<40;k++){ seed=seed*1103515245u+12345u; int bit=seed%size; buf[bit>>5]&=~(1u<<(bit&31)); } }
        seed=seed*1103515245u+12345u; unsigned start=seed%(size+50);
        seed=seed*1103515245u+12345u; unsigned len=seed%600;
        RTL_BITMAP bm={(unsigned long)size,buf};
        int y=sys(&bm,start,len)?1:0, o=wia_arebitsset(&bm,start,len)?1:0, r=ref_arebitsset(&bm,start,len)?1:0;
        if(o!=r||y!=r){ printf("FAIL start=%u len=%u: ntdll=%d ours=%d ref=%d\n",start,len,y,o,r); ++failures; }
    }
    if(!failures) printf("CORRECTNESS: PASS (arebitsset 200000 random start/len, holes, out-of-range, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
