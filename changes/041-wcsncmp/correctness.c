// changes/041-wcsncmp/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>
extern int wia_wcsncmp(const wchar_t*, const wchar_t*, size_t);
int ref_wcsncmp(const unsigned short*, const unsigned short*, size_t);
typedef int (__cdecl *fn)(const wchar_t*, const wchar_t*, size_t);
static int failures=0;
static int sgn(int x){ return (x>0)-(x<0); }
static void chk(int r,int o,int y,const char* what,size_t len,int off,size_t n){
    if(sgn(o)!=sgn(r)||sgn(y)!=sgn(r)){ printf("FAIL [%s] len=%zu off=%d n=%zu: ref=%d ours=%d sys=%d\n",
        what,len,off,n,r,o,y); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"wcsncmp");
    if(!sys){printf("no wcsncmp\n");return 2;}
    static wchar_t A[600], B[600];
    unsigned long seed=0x41abcu;
    for(size_t len=0; len<=280; ++len){
        for(int off=0; off<8; ++off){
            wchar_t* a=A+off; wchar_t* b=B+off;
            for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); a[i]=b[i]=(c?c:2); }
            a[len]=b[len]=0;
            // a family of n values around len
            size_t ns[]={0,1,2,7,8,9,15,16,17, len? len-1:0, len, len+1, len+8, 300};
            for(int k=0;k<(int)(sizeof(ns)/sizeof(ns[0]));++k){
                size_t n=ns[k];
                // (a) equal strings
                chk(ref_wcsncmp((unsigned short*)a,(unsigned short*)b,n), wia_wcsncmp(a,b,n), sys(a,b,n), "eq", len, off, n);
                // (b) a difference at a random position < len
                if(len){ size_t p=(seed>>3)%len; wchar_t save=b[p];
                    b[p]=(wchar_t)(a[p]+((seed&1)?1:-1)); if(!b[p])b[p]=1;
                    chk(ref_wcsncmp((unsigned short*)a,(unsigned short*)b,n), wia_wcsncmp(a,b,n), sys(a,b,n), "diff", len, off, n);
                    b[p]=save;
                }
                // (c) b terminates early (shorter)
                if(len>1){ size_t p=1+((seed>>7)%(len)); if(p<=len){ wchar_t save=b[p-1]; b[p-1]=0;
                    chk(ref_wcsncmp((unsigned short*)a,(unsigned short*)b,n), wia_wcsncmp(a,b,n), sys(a,b,n), "short", len, off, n);
                    b[p-1]=save; } }
            }
        }
    }
    // page-guard: both strings end right before a NOACCESS page, equal, n huge -> stop at terminator
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* b1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* b2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(b1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(b2+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=2; tail<=120; tail+=2){
        wchar_t* t1=(wchar_t*)(b1+pg-tail), *s1=(wchar_t*)(b1+pg-320);
        wchar_t* t2=(wchar_t*)(b2+pg-tail), *s2=(wchar_t*)(b2+pg-320);
        for(wchar_t* p=s1;p<t1;++p)*p=L'A'; *t1=0;
        for(wchar_t* p=s2;p<t2;++p)*p=L'A'; *t2=0;
        chk(ref_wcsncmp((unsigned short*)s1,(unsigned short*)s2,100000), wia_wcsncmp(s1,s2,100000), sys(s1,s2,100000),"pg-eq",0,0,100000);
        // differ at the last char before terminator
        *(t1-1)=L'B';
        chk(ref_wcsncmp((unsigned short*)s1,(unsigned short*)s2,100000), wia_wcsncmp(s1,s2,100000), sys(s1,s2,100000),"pg-diff",0,0,100000);
    }
    if(!failures) printf("CORRECTNESS: PASS (wcsncmp fuzz 0..280 x8 align x n{0..300}: eq/diff/short + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
