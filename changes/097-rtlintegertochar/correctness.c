// changes/097-rtlintegertochar/correctness.c
// Bit-exact vs live ntdll!RtlIntegerToChar: NTSTATUS + the exact bytes written into the
// buffer, over many (value, base, length) combinations incl invalid bases and overflow.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern LONG wia_itoc(ULONG, ULONG, LONG, char*);
typedef LONG (NTAPI *fn)(ULONG, ULONG, LONG, char*);
static fn sys;
static int failures=0;
static void chk(ULONG v, ULONG base, LONG len){
    char a[80], b[80];
    memset(a,0x7E,sizeof a); memset(b,0x7E,sizeof b);
    LONG ra=sys(v,base,len,a);
    LONG rb=wia_itoc(v,base,len,b);
    if(ra!=rb || memcmp(a,b,sizeof a)!=0){
        printf("FAIL v=%lu base=%lu len=%ld: sys st=%08lx ours st=%08lx\n  sys:",v,base,len,ra,rb);
        for(int i=0;i<16;i++)printf(" %02x",(unsigned char)a[i]);
        printf("\n  our:"); for(int i=0;i<16;i++)printf(" %02x",(unsigned char)b[i]);
        printf("\n"); ++failures;
    }
}
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlIntegerToChar");
    if(!sys){ printf("no RtlIntegerToChar\n"); return 2; }
    static const ULONG vals[]={0,1,2,7,8,9,10,15,16,63,64,99,100,255,256,1000,12345,65535,65536,
        999999999,1000000000,0x7FFFFFFF,0x80000000,0xFFFFFFFF,0xDEADBEEF,0xCAFEBABE};
    static const ULONG bases[]={0,2,8,10,16, 3,7,17,1};   // last four invalid
    for(int vi=0; vi<(int)(sizeof(vals)/sizeof(vals[0])); ++vi)
        for(int bi=0; bi<(int)(sizeof(bases)/sizeof(bases[0])); ++bi)
            for(LONG len=0; len<=40; ++len)
                chk(vals[vi], bases[bi], len);
    // random
    unsigned s=0x1234u;
    for(int i=0;i<200000;i++){
        s=s*1103515245u+12345u; ULONG v=(s<<9)^(s>>3)^(ULONG)i*2654435761u;
        static const ULONG B[]={2,8,10,16}; ULONG base=B[(s>>5)&3];
        LONG len=(LONG)((s>>7)%40);
        chk(v,base,len);
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlIntegerToChar: values x bases{0,2,8,10,16,invalid} x len 0..40 + 200k random, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
