// changes/039-strspn/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>
extern size_t wia_strspn(const char*, const char*);
size_t ref_strspn(const char*, const char*);
typedef size_t (__cdecl *fn)(const char*, const char*);
static int failures=0;
static void chk(size_t r,size_t o,size_t y,const char* what,size_t len,int off,int sl){
    if(o!=r||y!=r){ printf("FAIL [%s] len=%zu off=%d setlen=%d: ref=%zu ours=%zu sys=%zu\n",
        what,len,off,sl,r,o,y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"strspn");
    if(!sys){printf("no strspn\n");return 2;}
    static char buf[600], set[64];
    unsigned long seed=0x39abcu;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<8; ++off){
            char* s=buf+off;
            static const int sls[]={0,1,2,3,4,7,8,16,31,32,40};
            for(int si=0; si<(int)(sizeof(sls)/sizeof(sls[0])); ++si){
                int sl=sls[si];
                for(int k=0;k<sl;++k){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); set[k]=c?c:3; }
                set[sl]=0;
                for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u;
                    if(sl && (seed&3)) s[i]=set[(seed>>16)%sl];
                    else { char c=(char)((seed>>16)|1); s[i]=c?c:5; } }
                s[len]=0;
                chk(ref_strspn(s,set), wia_strspn(s,set), sys(s,set), "mix", len, off, sl);
                if(sl){ for(size_t i=0;i<len;++i) s[i]=set[i%sl]; s[len]=0;
                    chk(ref_strspn(s,set), wia_strspn(s,set), sys(s,set), "all-in", len, off, sl); }
            }
        }
    }
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    char inset[2]={'A',0};
    for(int tail=1; tail<=140; tail++){
        char* term=(char*)(base+pg-tail);
        char* start=(char*)(base+pg-400);
        for(char* p=start;p<term;++p)*p='A';
        *term=0;
        chk(ref_strspn(start,inset), wia_strspn(start,inset), sys(start,inset),"pg-all-in",0,0,tail);
        char none[2]={'Z',0};
        chk(ref_strspn(start,none), wia_strspn(start,none), sys(start,none),"pg-span0",0,0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (strspn fuzz 0..300 x8 align x set{0..40}, mix+all-in + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
