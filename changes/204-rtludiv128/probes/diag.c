#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef unsigned __int64 u64;
extern u64 wia_udiv128(u64,u64,u64,u64*);
u64 ref_udiv128(u64,u64,u64,u64*);
typedef u64 (NTAPI *FN)(u64,u64,u64,u64*);
int main(void){
    setvbuf(stdout,0,_IONBF,0);
    FN sys = (FN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlUdiv128");
    static const u64 V[] = { 0,1,2,3,4,7,8,15,16,255,256,0xFFFFull,0x10000ull,
        0x7FFFFFFFFFFFFFFFull,0x8000000000000000ull,0x8000000000000001ull,
        0xFFFFFFFFFFFFFFFEull,0xFFFFFFFFFFFFFFFFull,0x9E3779B97F4A7C15ull,
        0x5555555555555555ull,0xAAAAAAAAAAAAAAAAull,0x100000000ull,0xFFFFFFFFull };
    enum { NV = sizeof(V)/sizeof(V[0]) };
    int shown=0, bad=0;
    printf("%-18s %-18s %-18s | %-18s %-18s | %-18s %-18s | %-18s %-18s  region\n",
           "hi","lo","d","ours q","ours r","ref q","ref r","live q","live r");
    for(int a=0;a<NV;a++)for(int b=0;b<NV;b++)for(int c=0;c<NV;c++){
        u64 hi=V[a], lo=V[b], d=V[c];
        u64 ra=0xA5A5A5A5A5A5A5A5ull, rb=ra, rc=ra;
        u64 qa=wia_udiv128(hi,lo,d,&ra);
        u64 qb=ref_udiv128(hi,lo,d,&rb);
        u64 qc=sys(hi,lo,d,&rc);
        if(qa!=qb||ra!=rb||qa!=qc||ra!=rc){
            ++bad;
            if(shown<20){
                printf("%016llX %016llX %016llX | %016llX %016llX | %016llX %016llX | %016llX %016llX  %s\n",
                       hi,lo,d,qa,ra,qb,rb,qc,rc, (hi<d)?"div":"sat");
                ++shown;
            }
        }
    }
    printf("\n%d mismatches out of %d triples\n", bad, NV*NV*NV);
    return 0;
}
