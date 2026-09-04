// changes/040-strcspn/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>
extern size_t wia_strcspn(const char*, const char*);
size_t ref_strcspn(const char*, const char*);
typedef size_t (__cdecl *fn)(const char*, const char*);
static int failures=0;
static void chk(size_t r,size_t o,size_t y,const char* what,size_t len,int off,int sl){
    if(o!=r||y!=r){ printf("FAIL [%s] len=%zu off=%d setlen=%d: ref=%zu ours=%zu sys=%zu\n",
        what,len,off,sl,r,o,y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"strcspn");
    if(!sys){printf("no strcspn\n");return 2;}
    static char buf[600], set[64];
    unsigned long seed=0x40abcu;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<8; ++off){
            char* s=buf+off;
            static const int sls[]={0,1,2,3,4,7,8,16,31,32,40};
            for(int si=0; si<(int)(sizeof(sls)/sizeof(sls[0])); ++si){
                int sl=sls[si];
                for(int k=0;k<sl;++k){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); set[k]=c?c:3; }
                set[sl]=0;
                for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u;
                    if(sl && ((seed&3)==0)) s[i]=set[(seed>>16)%sl];
                    else { char c; do{ seed=seed*1103515245u+12345u; c=(char)((seed>>16)|1);}while(!c); s[i]=c; } }
                s[len]=0;
                chk(ref_strcspn(s,set), wia_strcspn(s,set), sys(s,set), "mix", len, off, sl);
                // no member -> full span (use bytes 0x80.. that won't collide with a small ascii-ish set often;
                // to be safe, actively strip any set member out)
                for(size_t i=0;i<len;++i){ char c=(char)(0x80+((i*7+off)&0x3f)); s[i]=c?c:0x7f; }
                for(size_t i=0;i<len;++i){ const char* p=set; while(*p && *p!=s[i]) ++p; if(*p) s[i]=0x7e; }
                s[len]=0;
                chk(ref_strcspn(s,set), wia_strcspn(s,set), sys(s,set), "no-member", len, off, sl);
            }
        }
    }
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    char set2[3]={0x42,0x43,0};
    for(int tail=1; tail<=140; tail++){
        char* term=(char*)(base+pg-tail);
        char* start=(char*)(base+pg-400);
        for(char* p=start;p<term;++p)*p='A';
        *term=0;
        chk(ref_strcspn(start,set2), wia_strcspn(start,set2), sys(start,set2),"pg-full",0,0,tail);
        char setA[2]={'A',0};
        chk(ref_strcspn(start,setA), wia_strcspn(start,setA), sys(start,setA),"pg-span0",0,0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (strcspn fuzz 0..300 x8 align x set{0..40}, mix+no-member + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
