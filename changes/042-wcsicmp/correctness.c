// changes/042-wcsicmp/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern int wia_wcsicmp(const wchar_t*, const wchar_t*);
int ref_wcsicmp(const unsigned short*, const unsigned short*);
typedef int (__cdecl *fn)(const wchar_t*, const wchar_t*);
static int failures=0;
static int sgn(int x){ return (x>0)-(x<0); }
static void chk(int r,int o,int y,const char* what,size_t len,int off){
    if(sgn(o)!=sgn(r)||sgn(y)!=sgn(r)){ printf("FAIL [%s] len=%zu off=%d: ref=%d ours=%d sys=%d\n",
        what,len,off,r,o,y); ++failures; }
}
// random wchar that may be ASCII letter (either case), other ascii, or non-ascii (>=0x80, incl 0xC0/0x410 letters)
static wchar_t randch(unsigned long* seed){
    *seed=*seed*1103515245u+12345u; unsigned r=*seed>>8; int p=r%10;
    if(p<4) return (wchar_t)(0x41+(r%26));      // A-Z
    if(p<8) return (wchar_t)(0x61+(r%26));      // a-z
    if(p<9) return (wchar_t)(0x20+(r%0x40));    // other ascii
    unsigned nz[]={0xC0,0xE0,0x410,0x430,0x1E9E,0xFF21,0x100,0x17F};
    return (wchar_t)nz[r%8];                     // non-ascii (must NOT fold)
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_wcsicmp");
    if(!sys){printf("no _wcsicmp\n");return 2;}
    static wchar_t A[600], B[600];
    unsigned long seed=0x42abcu;
    for(size_t len=0; len<=280; ++len){
        for(int off=0; off<8; ++off){
            wchar_t* a=A+off; wchar_t* b=B+off;
            for(size_t i=0;i<len;++i){ wchar_t c=randch(&seed); a[i]=c?c:1; }
            a[len]=0;
            // (a) b = a with random ASCII-letter case flips (must stay case-insensitively equal)
            for(size_t i=0;i<len;++i){ wchar_t c=a[i];
                if((c>=0x41&&c<=0x5A)||(c>=0x61&&c<=0x7A)){ seed=seed*1103515245u+12345u; if(seed&1) c^=0x20; }
                b[i]=c; }
            b[len]=0;
            chk(ref_wcsicmp((unsigned short*)a,(unsigned short*)b), wia_wcsicmp(a,b), sys(a,b), "case-eq", len, off);
            // (b) inject a real difference at a random position
            if(len){ size_t p=(seed>>3)%len; wchar_t save=b[p]; b[p]=randch(&seed); if(!b[p])b[p]=1;
                chk(ref_wcsicmp((unsigned short*)a,(unsigned short*)b), wia_wcsicmp(a,b), sys(a,b), "diff", len, off);
                b[p]=save; }
            // (c) b terminates early
            if(len>1){ size_t p=1+((seed>>5)%len); if(p<=len){ wchar_t save=b[p-1]; b[p-1]=0;
                chk(ref_wcsicmp((unsigned short*)a,(unsigned short*)b), wia_wcsicmp(a,b), sys(a,b), "short", len, off);
                b[p-1]=save; } }
        }
    }
    // page-guard: both end before a NOACCESS page, case-insensitively equal
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* p1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* p2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(p1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(p2+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=2; tail<=120; tail+=2){
        wchar_t* t1=(wchar_t*)(p1+pg-tail), *s1=(wchar_t*)(p1+pg-320);
        wchar_t* t2=(wchar_t*)(p2+pg-tail), *s2=(wchar_t*)(p2+pg-320);
        for(wchar_t* q=s1;q<t1;++q)*q=L'A'; *t1=0;
        for(wchar_t* q=s2;q<t2;++q)*q=L'a'; *t2=0;   // same letters, different case
        chk(ref_wcsicmp((unsigned short*)s1,(unsigned short*)s2), wia_wcsicmp(s1,s2), sys(s1,s2),"pg-eq",0,tail);
        *(t1-1)=L'C';
        chk(ref_wcsicmp((unsigned short*)s1,(unsigned short*)s2), wia_wcsicmp(s1,s2), sys(s1,s2),"pg-diff",0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (_wcsicmp fuzz 0..280 x8 align: case-eq/diff/short + non-ascii + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
