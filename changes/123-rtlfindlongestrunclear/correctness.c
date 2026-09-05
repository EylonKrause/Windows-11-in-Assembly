// changes/123-rtlfindlongestrunclear/correctness.c
// Bit-exact fuzz of wia_lrc vs live ntdll!RtlFindLongestRunClear + oracle: return length AND
// *StartingIndex, over explicit edges (all-clear, all-set, single-bit sweeps, empty) + 5M random fuzz
// across densities and lengths (incl. runs that span words and runs wholly inside one word).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned long wia_lrc(const RTL_BITMAP*, unsigned long*);
unsigned long ref_lrc(const RTL_BITMAP*, unsigned long*);
typedef ULONG (WINAPI *fn)(RTL_BITMAP*, PULONG);
static fn sys;
static int fails=0;
static void chk(const RTL_BITMAP* bm){
    unsigned long s1=0x11111111,s2=0x22222222,s3=0x33333333;
    unsigned long r1=sys((RTL_BITMAP*)bm,&s1), r2=wia_lrc(bm,&s2), r3=ref_lrc(bm,&s3);
    int ok=(r1==r2)&&(r1==r3)&&(s1==s2)&&(s1==s3);
    if(!ok){ if(fails<25) printf("FAIL nbits=%lu sys{len=%lu st=%lu} ours{len=%lu st=%lu} ref{len=%lu st=%lu}\n",
        bm->SizeOfBitMap,r1,s1,r2,s2,r3,s3); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlFindLongestRunClear");
    if(!sys){ printf("no RtlFindLongestRunClear\n"); return 2; }
    static unsigned long buf[600];
    { RTL_BITMAP bm={0,buf}; chk(&bm); }                                   // empty
    for(int i=0;i<600;i++)buf[i]=0;
    for(int nb=0;nb<=512;nb++){ RTL_BITMAP bm={(ULONG)nb,buf}; chk(&bm); } // all-clear
    for(int i=0;i<600;i++)buf[i]=0xFFFFFFFF;
    for(int nb=0;nb<=512;nb++){ RTL_BITMAP bm={(ULONG)nb,buf}; chk(&bm); } // all-set
    for(int pos=0;pos<192;pos++){ for(int i=0;i<600;i++)buf[i]=0; buf[pos>>5]|=1u<<(pos&31);
        RTL_BITMAP bm={192,buf}; chk(&bm); }                              // one set bit each position
    for(int pos=0;pos<192;pos++){ for(int i=0;i<600;i++)buf[i]=0xFFFFFFFF; buf[pos>>5]&=~(1u<<(pos&31));
        RTL_BITMAP bm={192,buf}; chk(&bm); }                              // one clear bit each position
    unsigned long seed=0x1234abcdu;
    for(int t=0;t<5000000;t++){
        seed=seed*1103515245u+12345u; int nb=(seed>>7)%4096;
        int nw=(nb+31)/32+2;
        for(int i=0;i<nw;i++){ seed=seed*1103515245u+12345u; unsigned long w=seed;
            int mode=(seed>>3)&7;
            if(mode<=1) w&=seed>>11;          // sparse set -> long clear runs
            else if(mode==2) w|=seed<<7;      // dense set -> short clear runs
            else if(mode==3) w=0;             // all clear word
            else if(mode==4) w=0xFFFFFFFF;    // all set word
            buf[i]=w; }
        RTL_BITMAP bm={(ULONG)nb,buf}; chk(&bm);
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlFindLongestRunClear vs live + oracle: length+StartingIndex, edges + 5M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
