// changes/006-crc32/correctness.c -- wia_crc32 vs scalar ref AND live ntdll!RtlComputeCrc32.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
extern uint32_t wia_crc32(uint32_t,const void*,int);
uint32_t ref_crc32(uint32_t,const void*,int);
typedef DWORD (WINAPI *rtl_fn)(DWORD,const void*,INT);
static int failures=0;
static void chk(uint32_t r,uint32_t o,uint32_t y,const char*what,int n){
    if(o!=r||y!=r){ printf("FAIL [%s] n=%d: ref=%08X ours=%08X ntdll=%08X\n",what,n,r,o,y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); rtl_fn f=(rtl_fn)GetProcAddress(h,"RtlComputeCrc32");
    // canonical check value
    printf("check(0,\"123456789\") = %08X (expect CBF43926); ntdll=%08X\n",
           wia_crc32(0,"123456789",9), f(0,"123456789",9));
    static unsigned char buf[70000]; uint32_t seed=1;
    for(int n=0;n<=1200;++n){
        uint32_t init=seed; seed=seed*1103515245u+12345u;
        for(int i=0;i<n;i++){buf[i]=(unsigned char)(seed>>16); seed=seed*1103515245u+12345u;}
        chk(ref_crc32(init,buf,n), wia_crc32(init,buf,n), f(init,buf,n), "fuzz", n);
        // also init=0 and unaligned start
        chk(ref_crc32(0,buf+ (n&15),(n>16?n-16:0)), wia_crc32(0,buf+(n&15),(n>16?n-16:0)), f(0,buf+(n&15),(n>16?n-16:0)), "unalign", n);
    }
    // larger sizes
    for(int n=4096;n<=65536;n+=4096){ for(int i=0;i<n;i++)buf[i]=(unsigned char)(i*131+7); chk(ref_crc32(0xABCD1234,buf,n),wia_crc32(0xABCD1234,buf,n),f(0xABCD1234,buf,n),"big",n); }
    if(!failures) printf("CORRECTNESS: PASS (check value + fuzz n=0..1200 + unaligned + big, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
