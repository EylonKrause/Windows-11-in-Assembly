// changes/089-strstr/correctness.c
// Bit-exact vs live ucrtbase!strstr AND the scalar oracle, over a fuzz of haystack
// lengths x alignments x needle lengths (present & absent), plus a page-guard case.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern char* wia_strstr(const char*, const char*);
char* ref_strstr(const char*, const char*);
typedef char* (__cdecl *fn)(const char*, const char*);
static int failures=0;
static void chk(char* r,char* o,char* y,const char* what,size_t len,int off,int nl){
    if(o!=r||y!=r){ printf("FAIL [%s] len=%zu off=%d needle=%d: ref=%p ours=%p sys=%p\n",
        what,len,off,nl,(void*)r,(void*)o,(void*)y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"strstr");
    if(!sys){printf("no strstr\n");return 2;}
    static char buf[900], ndl[64];
    unsigned long seed=0x51bad00du;
    // Small alphabet so needles actually collide with the haystack (exercises the
    // candidate-verify path, false positives, and near-terminator overlaps).
    static const char* A="abcd";
    for(size_t len=0; len<=400; ++len){
        for(int off=0; off<8; ++off){
            char* s=buf+off;
            for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u; s[i]=A[(seed>>16)&3]; }
            s[len]=0;
            static const int nls[]={0,1,2,3,4,5,7,8,13,16,31,32,40};
            for(int ni=0; ni<(int)(sizeof(nls)/sizeof(nls[0])); ++ni){
                int nl=nls[ni];
                // absent-ish random needle (small alphabet -> may or may not occur)
                for(int k=0;k<nl;++k){ seed=seed*1103515245u+12345u; ndl[k]=A[(seed>>16)&3]; }
                ndl[nl]=0;
                chk(ref_strstr(s,ndl), wia_strstr(s,ndl), sys(s,ndl), "rand", len, off, nl);
                // a guaranteed-present substring: copy a slice of the haystack as the needle
                if(len && nl && (size_t)nl<=len){
                    size_t pos=(seed>>4)%(len-(size_t)nl+1);
                    for(int k=0;k<nl;++k) ndl[k]=s[pos+k];
                    ndl[nl]=0;
                    chk(ref_strstr(s,ndl), wia_strstr(s,ndl), sys(s,ndl), "hit", len, off, nl);
                }
                // definitely-absent needle: characters outside the alphabet
                if(nl){ for(int k=0;k<nl;++k) ndl[k]='Z'; ndl[nl]=0;
                    chk(ref_strstr(s,ndl), wia_strstr(s,ndl), sys(s,ndl), "absent", len, off, nl); }
            }
            // empty needle -> haystack
            ndl[0]=0;
            chk(ref_strstr(s,ndl), wia_strstr(s,ndl), sys(s,ndl), "empty", len, off, 0);
        }
    }
    // page-guard: haystack ends right before a NOACCESS page; needle absent (full scan)
    // and needle whose first char matches at the very end but runs off the string.
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=140; tail++){
        char* term=(char*)(base+pg-tail);
        char* start=(char*)(base+pg-500);
        for(char* p=start;p<term;++p)*p='a';
        *term=0;
        char absent[4]={'a','a','Z',0};          // 'a' candidates everywhere, must not overrun
        chk(ref_strstr(start,absent), wia_strstr(start,absent), sys(start,absent),"pg-absent",0,0,tail);
        char run[8]="aaaaaaa";                    // needle longer than any remaining suffix at the tail
        chk(ref_strstr(start,run), wia_strstr(start,run), sys(start,run),"pg-run",0,0,tail);
        char hit[3]={'a','a',0};
        chk(ref_strstr(start,hit), wia_strstr(start,hit), sys(start,hit),"pg-hit",0,0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (strstr fuzz len0..400 x8 align x needle{0..40} rand/hit/absent + empty + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
