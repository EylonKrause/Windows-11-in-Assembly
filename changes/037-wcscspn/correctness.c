// changes/037-wcscspn/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>
extern size_t wia_wcscspn(const wchar_t*, const wchar_t*);
size_t ref_wcscspn(const unsigned short*, const unsigned short*);
typedef size_t (__cdecl *fn)(const wchar_t*, const wchar_t*);
static int failures=0;
static void chk(size_t r,size_t o,size_t y,const char* what,size_t len,int off,int sl){
    if(o!=r||y!=r){ printf("FAIL [%s] len=%zu off=%d setlen=%d: ref=%zu ours=%zu sys=%zu\n",
        what,len,off,sl,r,o,y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"wcscspn");
    if(!sys){printf("no wcscspn\n");return 2;}
    static wchar_t buf[512], set[64];
    unsigned long seed=0x37abcu;
    for(size_t len=0; len<=260; ++len){
        for(int off=0; off<8; ++off){
            wchar_t* s=buf+off;
            static const int sls[]={0,1,2,3,4,7,8,16,31,32,40};
            for(int si=0; si<(int)(sizeof(sls)/sizeof(sls[0])); ++si){
                int sl=sls[si];
                for(int k=0;k<sl;++k){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); set[k]=c?c:3; }
                set[sl]=0;
                // str: ~3/4 chars NOT in the set (long complement spans), ~1/4 a member (boundary)
                for(size_t i=0;i<len;++i){
                    seed=seed*1103515245u+12345u;
                    if(sl && ((seed&3)==0)) s[i]=set[(seed>>16)%sl];
                    else { wchar_t c; do{ seed=seed*1103515245u+12345u; c=(wchar_t)((seed>>16)|1);}while(!c); s[i]=c; }
                }
                s[len]=0;
                chk(ref_wcscspn((unsigned short*)s,(unsigned short*)set),
                    wia_wcscspn(s,set), sys(s,set), "mix", len, off, sl);
                // set present nowhere -> full span == len (also empty set)
                for(size_t i=0;i<len;++i) s[i]=(wchar_t)(0x100+((i*7+off)&0x7f)); s[len]=0; // 0x100..0x17f, disjoint from ascii set
                chk(ref_wcscspn((unsigned short*)s,(unsigned short*)set),
                    wia_wcscspn(s,set), sys(s,set), "no-member", len, off, sl);
            }
        }
    }
    // page-guard: str (no set member) ends right before a NOACCESS page -> full span, no over-read
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    wchar_t set2[4]={0x4242,0x4243,0}; // absent
    for(int tail=2; tail<=120; tail+=2){
        wchar_t* term=(wchar_t*)(base+pg-tail);
        wchar_t* start=(wchar_t*)(base+pg-320);
        for(wchar_t* p=start;p<term;++p)*p=L'A';
        *term=0;
        chk(ref_wcscspn((unsigned short*)start,(unsigned short*)set2),
            wia_wcscspn(start,set2), sys(start,set2),"pg-full",0,0,tail);
        // member = 'A' at position 0 -> span 0
        wchar_t setA[2]={L'A',0};
        chk(ref_wcscspn((unsigned short*)start,(unsigned short*)setA),
            wia_wcscspn(start,setA), sys(start,setA),"pg-span0",0,0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (wcscspn fuzz 0..260 x8 align x set{0..40}, mix+no-member + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
