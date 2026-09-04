// changes/046-memicmp/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>
extern int wia_memicmp(const void*, const void*, size_t);
int ref_memicmp(const unsigned char*, const unsigned char*, size_t);
typedef int (__cdecl *fn)(const void*, const void*, size_t);
static int failures=0;
static int sgn(int x){ return (x>0)-(x<0); }
static void chk(int r,int o,int y,const char* what,size_t n,int off){
    if(sgn(o)!=sgn(r)||sgn(y)!=sgn(r)){ printf("FAIL [%s] n=%zu off=%d: ref=%d ours=%d sys=%d\n",what,n,off,r,o,y); ++failures; }
}
static unsigned char randch(unsigned long* s){
    *s=*s*1103515245u+12345u; unsigned r=*s>>8; int p=r%10;
    if(p<4) return (unsigned char)(0x41+(r%26));
    if(p<8) return (unsigned char)(0x61+(r%26));
    if(p<9) return (unsigned char)(r%0x100);         // any byte incl 0
    return (unsigned char)(0x80+(r%0x80));
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_memicmp");
    if(!sys){printf("no _memicmp\n");return 2;}
    static unsigned char A[600], B[600];
    unsigned long seed=0x46abcu;
    for(size_t n=0; n<=300; ++n){
        for(int off=0; off<8; ++off){
            unsigned char* a=A+off; unsigned char* b=B+off;
            for(size_t i=0;i<n;++i){ a[i]=randch(&seed); }
            // b = a with random ASCII-letter case flips (must stay ci-equal)
            for(size_t i=0;i<n;++i){ unsigned char c=a[i];
                if((c>=0x41&&c<=0x5A)||(c>=0x61&&c<=0x7A)){ seed=seed*1103515245u+12345u; if(seed&1) c^=0x20; }
                b[i]=c; }
            chk(ref_memicmp(a,b,n), wia_memicmp(a,b,n), sys(a,b,n), "case-eq", n, off);
            // inject a difference at a random position (n>0)
            if(n){ size_t p=(seed>>3)%n; unsigned char save=b[p]; b[p]=randch(&seed);
                chk(ref_memicmp(a,b,n), wia_memicmp(a,b,n), sys(a,b,n), "diff", n, off); b[p]=save; }
            // include embedded NULs (memicmp does not stop at them)
            if(n>4){ a[n/2]=0; b[n/2]=0;
                chk(ref_memicmp(a,b,n), wia_memicmp(a,b,n), sys(a,b,n), "with-nul", n, off); }
        }
    }
    // page-guard: buffers end exactly at a NOACCESS page, n = full size -> must not read past a+n
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* p1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* p2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(p1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(p2+pg,pg,PAGE_NOACCESS,&old);
    for(int n=1; n<=200; n++){
        unsigned char* a=p1+pg-n; unsigned char* b=p2+pg-n;   // last n bytes before the guard page
        for(int i=0;i<n;i++){ a[i]=(unsigned char)('A'+(i%23)); b[i]=(unsigned char)('a'+(i%23)); }
        chk(ref_memicmp(a,b,(size_t)n), wia_memicmp(a,b,(size_t)n), sys(a,b,(size_t)n),"pg-eq",(size_t)n,0);
        a[n-1]='!';   // differ at the very last byte
        chk(ref_memicmp(a,b,(size_t)n), wia_memicmp(a,b,(size_t)n), sys(a,b,(size_t)n),"pg-diff",(size_t)n,0);
    }
    if(!failures) printf("CORRECTNESS: PASS (_memicmp fuzz n 0..300 x8 align: case-eq/diff/with-nul + >=0x80 + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
