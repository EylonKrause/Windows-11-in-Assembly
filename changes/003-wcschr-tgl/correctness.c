// changes/003-wcschr/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern wchar_t* wia_wcschr(const wchar_t*, wchar_t);
wchar_t* ref_wcschr(const wchar_t*, wchar_t);
typedef wchar_t* (__cdecl *fn)(const wchar_t*, wchar_t);
static int failures=0;
static void check(wchar_t* r, wchar_t* o, wchar_t* y, const char* what, size_t len, int off, int pos){
    if(o!=r||y!=r){ printf("FAIL [%s] len=%zu off=%d pos=%d: ref=%p ours=%p sys=%p\n",what,len,off,pos,(void*)r,(void*)o,(void*)y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"wcschr");
    if(!sys){printf("no wcschr\n");return 2;}
    static wchar_t buf[512];
    unsigned long seed=0x99991u;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<16; ++off){
            wchar_t* s=buf+off;
            for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); s[i]=c?c:2; }
            s[len]=0;
            // target absent (use a value we didn't insert: 0x5555 guaranteed? not guaranteed; force by scanning)
            wchar_t absent=0x4242; int clash=1;
            while(clash){ clash=0; for(size_t i=0;i<len;++i) if(s[i]==absent){absent++;clash=1;break;} }
            check(ref_wcschr(s,absent), wia_wcschr(s,absent), sys(s,absent), "absent", len, off, -1);
            // c==0 -> terminator
            check(ref_wcschr(s,0), wia_wcschr(s,0), sys(s,0), "zero", len, off, -1);
            // present at various positions
            for(size_t pos=0; pos<len; pos += (len>40?7:1)){
                wchar_t save=s[pos]; s[pos]=absent; // put the (previously absent) target here
                check(ref_wcschr(s,absent), wia_wcschr(s,absent), sys(s,absent), "present", len, off, (int)pos);
                s[pos]=save;
            }
        }
    }
    // page-guard: string ends at NOACCESS page, target absent -> must stop at terminator
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=2; tail<=80; tail+=2){
        wchar_t* term=(wchar_t*)(base+pg-tail);
        wchar_t* start=(wchar_t*)(base+pg-256);
        for(wchar_t* p=start;p<term;++p)*p=L'A';
        *term=0;
        check(ref_wcschr(start,0x4242), wia_wcschr(start,0x4242), sys(start,0x4242),"pg-absent",0,0,tail);
        check(ref_wcschr(start,0),      wia_wcschr(start,0),      sys(start,0),     "pg-zero",  0,0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (fuzz 0..300 x16, absent/zero/present + page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
