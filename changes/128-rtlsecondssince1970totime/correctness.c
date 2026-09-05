// changes/128-rtlsecondssince1970totime/correctness.c
// Bit-exact check of wia_secs2time vs live ntdll!RtlSecondsSince1970ToTime + oracle, over edges,
// every power-of-two boundary, a dense low sweep, and a stride across the whole ULONG domain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern void wia_secs2time(unsigned long, long long*);
void ref_secs2time(unsigned long, long long*);
typedef void (WINAPI *fn)(unsigned long, long long*);
static fn sys;
static int fails=0;
static void chk(unsigned long s){
    long long a=0x5A5A5A5A5A5A5A5ALL, b=0x3C3C3C3C3C3C3C3CLL, c=0x7777777777777777LL;
    sys(s,&a); wia_secs2time(s,&b); ref_secs2time(s,&c);
    if(a!=b || a!=c){ if(fails<20) printf("FAIL secs=%lu sys=%lld ours=%lld ref=%lld\n",s,a,b,c); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlSecondsSince1970ToTime");
    if(!sys){ printf("no RtlSecondsSince1970ToTime\n"); return 2; }
    chk(0); chk(1); chk(0xFFFFFFFFul); chk(0xFFFFFFFEul); chk(1700000000ul); chk(2147483647ul); chk(2147483648ul);
    for(int b=0;b<32;b++){ unsigned long v=1ul<<b; chk(v-1); chk(v); chk(v+1); }
    for(unsigned long s=0; s<2000000 && fails<20; s++) chk(s);
    for(unsigned long long s=0; s<4294967296ULL && fails<20; s+=1021) chk((unsigned long)s);
    if(!fails) printf("CORRECTNESS: PASS (RtlSecondsSince1970ToTime vs live + oracle: edges, all 2^k boundaries, 2M dense + 4.2M full-domain stride)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
