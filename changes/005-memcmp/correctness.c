// changes/005-memcmp/correctness.c: sign-compare vs scalar ref and live ucrtbase memcmp.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern int wia_memcmp(const void*, const void*, size_t);
int ref_memcmp(const void*, const void*, size_t);
typedef int (__cdecl *fn)(const void*, const void*, size_t);
static int failures=0;
static int sgn(int x){return (x>0)-(x<0);}
static void check(int r,int o,int y,const char* what,size_t n,int off,int pos){
    if(sgn(o)!=sgn(r)||sgn(y)!=sgn(r)){ printf("FAIL [%s] n=%zu off=%d pos=%d: ref=%d ours=%d sys=%d\n",what,n,off,pos,r,o,y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"memcmp");
    static unsigned char A[600],B[600];
    unsigned long seed=0x2468u;
    for(size_t n=0;n<=300;++n){
        for(int off=0;off<16;++off){
            unsigned char* a=A+off; unsigned char* b=B+off;
            for(size_t i=0;i<n;++i){ seed=seed*1103515245u+12345u; a[i]=b[i]=(unsigned char)(seed>>16); }
            check(ref_memcmp(a,b,n),wia_memcmp(a,b,n),sys(a,b,n),"equal",n,off,-1);
            for(size_t pos=0;pos<n;pos+=(n>40?5:1)){
                unsigned char sv=b[pos];
                b[pos]=(unsigned char)(a[pos]+1); check(ref_memcmp(a,b,n),wia_memcmp(a,b,n),sys(a,b,n),"gt",n,off,(int)pos);
                b[pos]=(unsigned char)(a[pos]-1); check(ref_memcmp(a,b,n),wia_memcmp(a,b,n),sys(a,b,n),"lt",n,off,(int)pos);
                b[pos]=sv;
            }
        }
    }
    // bounded page-guard: both buffers end at NOACCESS, equal -> full compare to p+n, no fault.
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* B1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* B2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(B1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(B2+pg,pg,PAGE_NOACCESS,&old);
    for(size_t n=1;n<=48;++n){
        unsigned char* a=B1+pg-n; unsigned char* b=B2+pg-n;
        for(size_t i=0;i<n;++i){a[i]=b[i]=0x33;}
        check(ref_memcmp(a,b,n),wia_memcmp(a,b,n),sys(a,b,n),"pg-equal",n,0,-1);
        b[n-1]=0x44; check(ref_memcmp(a,b,n),wia_memcmp(a,b,n),sys(a,b,n),"pg-lastdiff",n,0,(int)(n-1)); b[n-1]=0x33;
    }
    if(!failures) printf("CORRECTNESS: PASS (equal/gt/lt fuzz 0..300 x16 + page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
