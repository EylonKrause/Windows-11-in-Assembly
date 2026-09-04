// changes/038-strpbrk/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern char* wia_strpbrk(const char*, const char*);
char* ref_strpbrk(const char*, const char*);
typedef char* (__cdecl *fn)(const char*, const char*);
static int failures=0;
static void chk(char* r,char* o,char* y,const char* what,size_t len,int off,int sl){
    if(o!=r||y!=r){ printf("FAIL [%s] len=%zu off=%d setlen=%d: ref=%p ours=%p sys=%p\n",
        what,len,off,sl,(void*)r,(void*)o,(void*)y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"strpbrk");
    if(!sys){printf("no strpbrk\n");return 2;}
    static char buf[600], set[64];
    unsigned long seed=0x38abcu;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<8; ++off){
            char* s=buf+off;
            for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); s[i]=c?c:2; }
            s[len]=0;
            static const int sls[]={0,1,2,3,4,7,8,16,31,32,40};
            for(int si=0; si<(int)(sizeof(sls)/sizeof(sls[0])); ++si){
                int sl=sls[si];
                for(int k=0;k<sl;++k){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); set[k]=c?c:3; }
                set[sl]=0;
                chk(ref_strpbrk(s,set), wia_strpbrk(s,set), sys(s,set), "rand", len, off, sl);
                if(len && sl){ size_t pos=(seed>>3)%len; char save=s[pos];
                    s[pos]=set[(seed>>5)%sl];
                    chk(ref_strpbrk(s,set), wia_strpbrk(s,set), sys(s,set), "hit", len, off, sl);
                    s[pos]=save; }
            }
        }
    }
    // page-guard: str ends right before a NOACCESS page; set absent -> stop at terminator
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    char absent[4]={0x42,0x43,0x44,0};
    for(int tail=1; tail<=140; tail++){
        char* term=(char*)(base+pg-tail);
        char* start=(char*)(base+pg-400);
        for(char* p=start;p<term;++p)*p='A';
        *term=0;
        chk(ref_strpbrk(start,absent), wia_strpbrk(start,absent), sys(start,absent),"pg-absent",0,0,tail);
        char hit[2]={'A',0};
        chk(ref_strpbrk(start,hit), wia_strpbrk(start,hit), sys(start,hit),"pg-hit",0,0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (strpbrk fuzz 0..300 x8 align x set{0..40} + hit + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
