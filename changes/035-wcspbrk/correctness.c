// changes/035-wcspbrk/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern wchar_t* wia_wcspbrk(const wchar_t*, const wchar_t*);
wchar_t* ref_wcspbrk(const wchar_t*, const wchar_t*);
typedef wchar_t* (__cdecl *fn)(const wchar_t*, const wchar_t*);
static int failures=0;
static void chk(wchar_t* r, wchar_t* o, wchar_t* y, const char* what, size_t len, int off, int sl){
    if(o!=r||y!=r){ printf("FAIL [%s] len=%zu off=%d setlen=%d: ref=%p ours=%p sys=%p\n",
        what,len,off,sl,(void*)r,(void*)o,(void*)y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"wcspbrk");
    if(!sys){printf("no wcspbrk\n");return 2;}
    static wchar_t buf[512], set[64];
    unsigned long seed=0x51234u;
    // vary str length, alignment, and set (size + membership)
    for(size_t len=0; len<=260; ++len){
        for(int off=0; off<8; ++off){
            wchar_t* s=buf+off;
            for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); s[i]=c?c:2; }
            s[len]=0;
            // several set sizes incl the scalar-fallback boundary (>=32)
            static const int sls[]={0,1,2,3,4,7,8,16,31,32,40};
            for(int si=0; si<(int)(sizeof(sls)/sizeof(sls[0])); ++si){
                int sl=sls[si];
                for(int k=0;k<sl;++k){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); set[k]=c?c:3; }
                set[sl]=0;
                // (a) set as-built (mostly absent from s -> full scan)
                chk(ref_wcspbrk(s,set), wia_wcspbrk(s,set), sys(s,set), "rand-set", len, off, sl);
                // (b) force a member of the set into s at a known position, if both nonempty
                if(len && sl){
                    size_t pos=(seed>>3)%len; wchar_t save=s[pos];
                    s[pos]=set[(seed>>5)%sl];
                    chk(ref_wcspbrk(s,set), wia_wcspbrk(s,set), sys(s,set), "hit", len, off, sl);
                    s[pos]=save;
                }
            }
        }
    }
    // page-guard: str ends right before a NOACCESS page; set absent -> must stop at terminator
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    wchar_t absent_set[4]={0x4242,0x4243,0x4244,0}; // values not written into the run of 'A'
    for(int tail=2; tail<=120; tail+=2){
        wchar_t* term=(wchar_t*)(base+pg-tail);
        wchar_t* start=(wchar_t*)(base+pg-320);
        for(wchar_t* p=start;p<term;++p)*p=L'A';
        *term=0;
        chk(ref_wcspbrk(start,absent_set), wia_wcspbrk(start,absent_set), sys(start,absent_set),"pg-absent",0,0,tail);
        // and a set that matches 'A' at the very last char before terminator
        wchar_t hit_set[2]={L'A',0};
        chk(ref_wcspbrk(start,hit_set), wia_wcspbrk(start,hit_set), sys(start,hit_set),"pg-hit",0,0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (wcspbrk fuzz 0..260 x8 align x set{0..40} + hit + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
