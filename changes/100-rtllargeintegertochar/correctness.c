// changes/100-rtllargeintegertochar/correctness.c
// Bit-exact vs live ntdll!RtlLargeIntegerToChar: NTSTATUS + the exact bytes written into
// the buffer, over many (value, base, length) combinations incl invalid bases and overflow.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern LONG wia_litoc(LARGE_INTEGER*, ULONG, LONG, char*);
typedef LONG (NTAPI *fn)(LARGE_INTEGER*, ULONG, LONG, char*);
static fn sys;
static int failures=0;
static void chk(unsigned long long v, ULONG base, LONG len){
    LARGE_INTEGER a; a.QuadPart=(LONGLONG)v;
    LARGE_INTEGER b; b.QuadPart=(LONGLONG)v;
    char ba[96], bb[96];
    memset(ba,0x7E,sizeof ba); memset(bb,0x7E,sizeof bb);
    LONG ra=sys(&a,base,len,ba);
    LONG rb=wia_litoc(&b,base,len,bb);
    if(ra!=rb || memcmp(ba,bb,sizeof ba)!=0){
        printf("FAIL v=%llu base=%lu len=%ld: sys st=%08lx ours st=%08lx\n  sys:",v,base,len,ra,rb);
        for(int i=0;i<24;i++)printf(" %02x",(unsigned char)ba[i]);
        printf("\n  our:"); for(int i=0;i<24;i++)printf(" %02x",(unsigned char)bb[i]);
        printf("\n"); ++failures;
    }
}
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlLargeIntegerToChar");
    if(!sys){ printf("no RtlLargeIntegerToChar\n"); return 2; }
    static const unsigned long long vals[]={
        0,1,2,7,8,9,10,15,16,63,64,99,100,255,256,1000,12345,65535,65536,
        999999999ULL,1000000000ULL,0x7FFFFFFFULL,0x80000000ULL,0xFFFFFFFFULL,
        4294967296ULL,1234567890123ULL,999999999999999999ULL,10000000000000000000ULL,
        0x7FFFFFFFFFFFFFFFULL,0x8000000000000000ULL,0xFFFFFFFFFFFFFFFFULL,
        0xFEDCBA9876543210ULL,0x0123456789ABCDEFULL,0xDEADBEEFCAFEBABEULL};
    static const ULONG bases[]={0,2,8,10,16, 3,7,17,1};   // last four invalid
    for(int vi=0; vi<(int)(sizeof(vals)/sizeof(vals[0])); ++vi)
        for(int bi=0; bi<(int)(sizeof(bases)/sizeof(bases[0])); ++bi)
            for(LONG len=0; len<=72; ++len)
                chk(vals[vi], bases[bi], len);
    // random
    unsigned long long s=0x1234567ULL;
    for(int i=0;i<300000;i++){
        s=s*6364136223846793005ULL+1442695040888963407ULL;
        unsigned long long v=s ^ (s>>29);
        static const ULONG B[]={2,8,10,16}; ULONG base=B[(s>>5)&3];
        LONG len=(LONG)((s>>7)%72);
        chk(v,base,len);
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlLargeIntegerToChar: 64-bit values x bases{0,2,8,10,16,invalid} x len 0..72 + 300k random, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
