// changes/036-wcsspn/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>
extern size_t wia_wcsspn(const wchar_t*, const wchar_t*);
size_t ref_wcsspn(const unsigned short*, const unsigned short*);
typedef size_t (__cdecl *fn)(const wchar_t*, const wchar_t*);
static int failures=0;
static void chk(size_t r,size_t o,size_t y,const char* what,size_t len,int off,int sl){
    if(o!=r||y!=r){ printf("FAIL [%s] len=%zu off=%d setlen=%d: ref=%zu ours=%zu sys=%zu\n",
        what,len,off,sl,r,o,y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"wcsspn");
    if(!sys){printf("no wcsspn\n");return 2;}
    static wchar_t buf[512], set[64];
    unsigned long seed=0x71234u;
    for(size_t len=0; len<=260; ++len){
        for(int off=0; off<8; ++off){
            wchar_t* s=buf+off;
            static const int sls[]={0,1,2,3,4,7,8,16,31,32,40};
            for(int si=0; si<(int)(sizeof(sls)/sizeof(sls[0])); ++si){
                int sl=sls[si];
                for(int k=0;k<sl;++k){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); set[k]=c?c:3; }
                set[sl]=0;
                // build str: each char is, with prob ~3/4, drawn from the set (long spans),
                // else a random (likely non-member) char -> exercises the stop at the boundary
                for(size_t i=0;i<len;++i){
                    seed=seed*1103515245u+12345u;
                    if(sl && (seed&3)) s[i]=set[(seed>>16)%sl];
                    else { wchar_t c=(wchar_t)((seed>>16)|1); s[i]=c?c:5; }
                }
                s[len]=0;
                chk(ref_wcsspn((unsigned short*)s,(unsigned short*)set),
                    wia_wcsspn(s,set), sys(s,set), "mix", len, off, sl);
                // all-in-set (full span to terminator), if set nonempty
                if(sl){ for(size_t i=0;i<len;++i) s[i]=set[i%sl]; s[len]=0;
                    chk(ref_wcsspn((unsigned short*)s,(unsigned short*)set),
                        wia_wcsspn(s,set), sys(s,set), "all-in", len, off, sl); }
            }
        }
    }
    // page-guard: str all-in-set ends right before a NOACCESS page (must stop at terminator)
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    wchar_t inset[2]={L'A',0};
    for(int tail=2; tail<=120; tail+=2){
        wchar_t* term=(wchar_t*)(base+pg-tail);
        wchar_t* start=(wchar_t*)(base+pg-320);
        for(wchar_t* p=start;p<term;++p)*p=L'A';   // all members of {A}
        *term=0;
        chk(ref_wcsspn((unsigned short*)start,(unsigned short*)inset),
            wia_wcsspn(start,inset), sys(start,inset),"pg-all-in",0,0,tail);
        // and a set NOT containing 'A' -> span 0 at start
        wchar_t none[2]={L'Z',0};
        chk(ref_wcsspn((unsigned short*)start,(unsigned short*)none),
            wia_wcsspn(start,none), sys(start,none),"pg-span0",0,0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (wcsspn fuzz 0..260 x8 align x set{0..40}, mix+all-in + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
