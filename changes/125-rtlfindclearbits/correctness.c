// changes/125-rtlfindclearbits/correctness.c
// Bit-exact fuzz of wia_findclearbits vs live ntdll!RtlFindClearBits + oracle, over explicit edges
// (empty/all-clear/all-set, num 0..n+2, hints incl. >= n) + 5M random fuzz across densities, lengths,
// NumberToFind and HintIndex.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned long wia_findclearbits(const RTL_BITMAP*, unsigned long, unsigned long);
unsigned long ref_findclearbits(const RTL_BITMAP*, unsigned long, unsigned long);
typedef ULONG (WINAPI *fn)(RTL_BITMAP*, ULONG, ULONG);
static fn sys;
static int fails=0;
static void chk(const RTL_BITMAP* bm, unsigned long num, unsigned long hint){
    unsigned long r1=sys((RTL_BITMAP*)bm,num,hint), r2=wia_findclearbits(bm,num,hint), r3=ref_findclearbits(bm,num,hint);
    if(r1!=r2 || r1!=r3){ if(fails<25) printf("FAIL n=%lu num=%lu hint=%lu sys=%lx ours=%lx ref=%lx\n",
        bm->SizeOfBitMap,num,hint,r1,r2,r3); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlFindClearBits");
    if(!sys){ printf("no RtlFindClearBits\n"); return 2; }
    static unsigned long buf[400];
    for(int i=0;i<400;i++)buf[i]=0;
    { RTL_BITMAP bm={0,buf}; for(ULONG num=0;num<=3;num++) for(ULONG hint=0;hint<=3;hint++) chk(&bm,num,hint); }
    for(int nb=1;nb<=200 && fails<25;nb++){ RTL_BITMAP bm={(ULONG)nb,buf};
        for(ULONG num=0;num<=(ULONG)nb+2;num++) for(ULONG hint=0;hint<=(ULONG)nb+2;hint+=5) chk(&bm,num,hint); }
    for(int i=0;i<400;i++)buf[i]=0xFFFFFFFF;
    for(int nb=1;nb<=200 && fails<25;nb++){ RTL_BITMAP bm={(ULONG)nb,buf};
        for(ULONG num=0;num<=4;num++) for(ULONG hint=0;hint<=(ULONG)nb;hint+=3) chk(&bm,num,hint); }
    unsigned long seed=0xc1ea7b17u;
    for(int t=0;t<5000000;t++){
        seed=seed*1103515245u+12345u; int nb=1+((seed>>7)%900);
        int nw=(nb+31)/32+2;
        for(int i=0;i<nw;i++){ seed=seed*1103515245u+12345u; unsigned long w=seed;
            int mode=(seed>>3)&7; if(mode<=1)w&=seed>>9; else if(mode==2)w|=seed<<7; else if(mode==3)w=0; else if(mode==4)w=0xFFFFFFFF; buf[i]=w; }
        seed=seed*1103515245u+12345u; ULONG num=(seed>>5)%70;
        seed=seed*1103515245u+12345u; ULONG hint=(seed>>5)%((unsigned)nb+16);
        RTL_BITMAP bm={(ULONG)nb,buf}; chk(&bm,num,hint);
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlFindClearBits vs live + oracle: edges + 5M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
