/* i2u_check.c -- do 052 and 053 actually write the terminator their headers describe?
 * descterm.c showed the exports need MaximumLength >= Length+2 and write a WCHAR NUL at
 * [Length/2]. 052's header already states the size rule. A stated rule is not an implemented
 * one, so this compares OURS against the live export over the whole interesting range,
 * whole buffer included. */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
extern NTSTATUS_ wia_itos(ULONG, ULONG, USTR*);
extern NTSTATUS_ wia_itos64(ULONGLONG, ULONG, USTR*);
extern void wia_dec2_init(void);
typedef NTSTATUS_ (NTAPI *f32)(ULONG, ULONG, USTR*);
typedef NTSTATUS_ (NTAPI *f64)(ULONGLONG, ULONG, USTR*);
#define CAP 80
#define NP 0x71
static int bad=0, total=0;
static void chk(f32 s32, f64 s64, ULONG v, ULONG base, int maxb){
    unsigned char b1[CAP], b2[CAP]; USTR d1,d2; NTSTATUS_ r1,r2; int k;
    memset(b1,NP,CAP); memset(b2,NP,CAP);
    d1.Length=0x5A5A; d1.MaximumLength=(USHORT)maxb; d1.Buffer=(WCHAR*)b1;
    d2.Length=0x5A5A; d2.MaximumLength=(USHORT)maxb; d2.Buffer=(WCHAR*)b2;
    r1=s32(v,base,&d1); r2=wia_itos(v,base,&d2); ++total;
    if(r1!=r2||d1.Length!=d2.Length||memcmp(b1,b2,CAP)){
        ++bad; if(bad<=6){
            printf("  32 DIFF v=%lu base=%lu Max=%d: live %08lX/%u ours %08lX/%u\n",
                   (unsigned long)v,(unsigned long)base,maxb,
                   (unsigned long)r1,d1.Length,(unsigned long)r2,d2.Length);
            printf("     live:"); for(k=0;k<14;k++) printf(" %02X",b1[k]);
            printf("\n     ours:"); for(k=0;k<14;k++) printf(" %02X",b2[k]); printf("\n"); } }
    memset(b1,NP,CAP); memset(b2,NP,CAP);
    d1.Length=0x5A5A; d1.MaximumLength=(USHORT)maxb; d1.Buffer=(WCHAR*)b1;
    d2.Length=0x5A5A; d2.MaximumLength=(USHORT)maxb; d2.Buffer=(WCHAR*)b2;
    r1=s64(v,base,&d1); r2=wia_itos64(v,base,&d2); ++total;
    if(r1!=r2||d1.Length!=d2.Length||memcmp(b1,b2,CAP)){
        ++bad; if(bad<=6) printf("  64 DIFF v=%lu base=%lu Max=%d: live %08lX/%u ours %08lX/%u\n",
               (unsigned long)v,(unsigned long)base,maxb,
               (unsigned long)r1,d1.Length,(unsigned long)r2,d2.Length); }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    f32 s32=(f32)GetProcAddress(h,"RtlIntegerToUnicodeString");
    f64 s64=(f64)GetProcAddress(h,"RtlInt64ToUnicodeString");
    static const ULONG BASES[4]={10,16,8,2};
    static const ULONG VALS[10]={0,1,7,9,10,99,1234,65535,1000000,4294967295u};
    int bi,vi,m;
    wia_dec2_init();
    if(!s32||!s64){ printf("resolve failed\n"); return 2; }
    for(bi=0;bi<4;++bi) for(vi=0;vi<10;++vi) for(m=0;m<=72;++m) chk(s32,s64,VALS[vi],BASES[bi],m);
    printf("%s: %d of %d comparisons differ (value x base x EVERY MaximumLength 0..72,\n"
           "  whole %d-byte buffer compared)\n", bad?"DIFFERENCES":"CLEAN", bad, total, CAP);
    return bad?1:0;
}
