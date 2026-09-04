// changes/044-wcsnicmp/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>
extern int wia_wcsnicmp(const wchar_t*, const wchar_t*, size_t);
int ref_wcsnicmp(const unsigned short*, const unsigned short*, size_t);
typedef int (__cdecl *fn)(const wchar_t*, const wchar_t*, size_t);
static int failures=0;
static int sgn(int x){ return (x>0)-(x<0); }
static void chk(int r,int o,int y,const char* what,size_t len,int off,size_t n){
    if(sgn(o)!=sgn(r)||sgn(y)!=sgn(r)){ printf("FAIL [%s] len=%zu off=%d n=%zu: ref=%d ours=%d sys=%d\n",what,len,off,n,r,o,y); ++failures; }
}
static wchar_t randch(unsigned long* s){
    *s=*s*1103515245u+12345u; unsigned r=*s>>8; int p=r%10;
    if(p<4) return (wchar_t)(0x41+(r%26));
    if(p<8) return (wchar_t)(0x61+(r%26));
    if(p<9) return (wchar_t)(0x20+(r%0x40));
    unsigned nz[]={0xC0,0xE0,0x410,0x430,0x1E9E,0xFF21,0x100,0x17F};
    return (wchar_t)nz[r%8];
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_wcsnicmp");
    if(!sys){printf("no _wcsnicmp\n");return 2;}
    static wchar_t A[600], B[600];
    unsigned long seed=0x44abcu;
    for(size_t len=0; len<=280; ++len){
        for(int off=0; off<8; ++off){
            wchar_t* a=A+off; wchar_t* b=B+off;
            for(size_t i=0;i<len;++i){ wchar_t c=randch(&seed); a[i]=c?c:1; }
            a[len]=0;
            for(size_t i=0;i<len;++i){ wchar_t c=a[i];
                if((c>=0x41&&c<=0x5A)||(c>=0x61&&c<=0x7A)){ seed=seed*1103515245u+12345u; if(seed&1) c^=0x20; }
                b[i]=c; }
            b[len]=0;
            size_t ns[]={0,1,2,7,8,9,15,16,17, len?len-1:0, len, len+1, len+8, 300};
            for(int k=0;k<(int)(sizeof(ns)/sizeof(ns[0]));++k){
                size_t n=ns[k];
                chk(ref_wcsnicmp((unsigned short*)a,(unsigned short*)b,n), wia_wcsnicmp(a,b,n), sys(a,b,n), "case-eq", len, off, n);
                if(len){ size_t p=(seed>>3)%len; wchar_t save=b[p]; b[p]=randch(&seed); if(!b[p])b[p]=1;
                    chk(ref_wcsnicmp((unsigned short*)a,(unsigned short*)b,n), wia_wcsnicmp(a,b,n), sys(a,b,n), "diff", len, off, n); b[p]=save; }
                if(len>1){ size_t p=1+((seed>>7)%len); if(p<=len){ wchar_t save=b[p-1]; b[p-1]=0;
                    chk(ref_wcsnicmp((unsigned short*)a,(unsigned short*)b,n), wia_wcsnicmp(a,b,n), sys(a,b,n), "short", len, off, n); b[p-1]=save; } }
            }
        }
    }
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* p1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* p2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(p1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(p2+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=2; tail<=120; tail+=2){
        wchar_t* t1=(wchar_t*)(p1+pg-tail), *s1=(wchar_t*)(p1+pg-320);
        wchar_t* t2=(wchar_t*)(p2+pg-tail), *s2=(wchar_t*)(p2+pg-320);
        for(wchar_t* q=s1;q<t1;++q)*q=L'A'; *t1=0;
        for(wchar_t* q=s2;q<t2;++q)*q=L'a'; *t2=0;
        chk(ref_wcsnicmp((unsigned short*)s1,(unsigned short*)s2,100000), wia_wcsnicmp(s1,s2,100000), sys(s1,s2,100000),"pg-eq",0,0,100000);
        *(t1-1)=L'C';
        chk(ref_wcsnicmp((unsigned short*)s1,(unsigned short*)s2,100000), wia_wcsnicmp(s1,s2,100000), sys(s1,s2,100000),"pg-diff",0,0,100000);
    }
    if(!failures) printf("CORRECTNESS: PASS (_wcsnicmp fuzz 0..280 x8 align x n{0..300}: case-eq/diff/short + non-ascii + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
