// changes/099-strncmp/correctness.c
// Bit-exact vs live ucrtbase!strncmp AND the scalar oracle, over a fuzz of lengths x
// alignments x n with planted differences/terminators, plus a page-guard.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern int wia_strncmp(const char*, const char*, size_t);
int ref_strncmp(const char*, const char*, size_t);
typedef int (__cdecl *fn)(const char*, const char*, size_t);
static int failures=0;
// strncmp's contract defines only the SIGN of the result; ucrtbase is itself inconsistent
// (full byte-difference in its scalar prefix, sign-only in its SWAR bulk path). So
// behavior-identical == same sign. Our impl returns the full difference (always the right
// sign) and matches the oracle exactly.
static int sgn(int x){ return (x>0)-(x<0); }
static void chk(const char* a, const char* b, size_t n, const char* what, fn sys){
    int r=ref_strncmp(a,b,n), o=wia_strncmp(a,b,n), y=sys(a,b,n);
    if(o!=r || sgn(y)!=sgn(r)){
        printf("FAIL [%s] n=%zu: ref=%d ours=%d sys=%d\n",what,n,r,o,y); ++failures;
    }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"strncmp");
    if(!sys){ printf("no strncmp\n"); return 2; }
    static char A[600], B[600];
    unsigned long seed=0x99abc;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<8; ++off){
            char* a=A+off; char* b=B+off;
            for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); a[i]=c; b[i]=c; }
            a[len]=0; b[len]=0;
            static const size_t ns[]={0,1,2,3,4,7,8,15,16,17,31,32,33,64,100,300,301};
            for(int ni=0; ni<(int)(sizeof(ns)/sizeof(ns[0])); ++ni){
                size_t n=ns[ni];
                chk(a,b,n,"equal",sys);
                if(len){ size_t pos=(seed>>3)%len; char save=b[pos]; b[pos]^=0x20; if(!b[pos])b[pos]=1;
                    chk(a,b,n,"diff",sys); b[pos]=save; }
                if(len){ size_t pos=(seed>>5)%len; char save=a[pos]; a[pos]=0;   // early terminator in a
                    chk(a,b,n,"term",sys); a[pos]=save; }
            }
        }
    }
    // page-guard: both strings end right before a NOACCESS page; equal up to terminator
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* b1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* b2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(b1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(b2+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=140; ++tail){
        char* s1=(char*)(b1+pg-tail); char* s2=(char*)(b2+pg-tail);
        for(int k=0;k<tail-1;k++){ s1[k]='A'; s2[k]='A'; }
        s1[tail-1]=0; s2[tail-1]=0;
        chk(s1,s2,1000,"pg-equal",sys);
        if(tail>2){ s2[tail-2]='B'; chk(s1,s2,1000,"pg-diff",sys); s2[tail-2]='A'; }
    }
    if(!failures) printf("CORRECTNESS: PASS (strncmp fuzz len0..300 x8 align x n{0..301} equal/diff/term + page-guard, vs ucrtbase + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
