// changes/007-rtlcomparememory/correctness.c -- vs scalar ref AND live ntdll!RtlCompareMemory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern size_t wia_rtlcmpmem(const void*,const void*,size_t);
size_t ref_rtlcmpmem(const void*,const void*,size_t);
typedef SIZE_T (WINAPI *fn)(const void*,const void*,SIZE_T);
static int failures=0;
static void chk(size_t r,size_t o,size_t y,const char*w,size_t n,int off,int pos){
    if(o!=r||y!=r){ printf("FAIL [%s] n=%zu off=%d pos=%d: ref=%zu ours=%zu ntdll=%zu\n",w,n,off,pos,r,o,y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlCompareMemory");
    static unsigned char A[600],B[600];
    unsigned long seed=0x77u;
    for(size_t n=0;n<=300;++n) for(int off=0;off<16;++off){
        unsigned char* a=A+off,*b=B+off;
        for(size_t i=0;i<n;i++){seed=seed*1103515245u+12345u; a[i]=b[i]=(unsigned char)(seed>>16);}
        chk(ref_rtlcmpmem(a,b,n),wia_rtlcmpmem(a,b,n),sys(a,b,n),"equal",n,off,-1);
        for(size_t pos=0;pos<n;pos+=(n>40?5:1)){ unsigned char sv=b[pos]; b[pos]^=0xFF;
            chk(ref_rtlcmpmem(a,b,n),wia_rtlcmpmem(a,b,n),sys(a,b,n),"diff",n,off,(int)pos); b[pos]=sv; }
    }
    // page-guard bounded
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* B1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* B2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(B1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(B2+pg,pg,PAGE_NOACCESS,&old);
    for(size_t n=1;n<=48;++n){ unsigned char* a=B1+pg-n,*b=B2+pg-n; for(size_t i=0;i<n;i++)a[i]=b[i]=0x5A;
        chk(ref_rtlcmpmem(a,b,n),wia_rtlcmpmem(a,b,n),sys(a,b,n),"pg-eq",n,0,-1);
        b[n-1]^=1; chk(ref_rtlcmpmem(a,b,n),wia_rtlcmpmem(a,b,n),sys(a,b,n),"pg-last",n,0,(int)(n-1)); b[n-1]^=1; }
    if(!failures) printf("CORRECTNESS: PASS (equal/diff fuzz 0..300 x16 + page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
