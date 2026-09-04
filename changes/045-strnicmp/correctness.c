// changes/045-strnicmp/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>
extern int wia_strnicmp(const char*, const char*, size_t);
int ref_strnicmp(const unsigned char*, const unsigned char*, size_t);
typedef int (__cdecl *fn)(const char*, const char*, size_t);
static int failures=0;
static int sgn(int x){ return (x>0)-(x<0); }
static void chk(int r,int o,int y,const char* what,size_t len,int off,size_t n){
    if(sgn(o)!=sgn(r)||sgn(y)!=sgn(r)){ printf("FAIL [%s] len=%zu off=%d n=%zu: ref=%d ours=%d sys=%d\n",what,len,off,n,r,o,y); ++failures; }
}
static unsigned char randch(unsigned long* s){
    *s=*s*1103515245u+12345u; unsigned r=*s>>8; int p=r%10;
    if(p<4) return (unsigned char)(0x41+(r%26));
    if(p<8) return (unsigned char)(0x61+(r%26));
    if(p<9) return (unsigned char)(0x20+(r%0x40));
    return (unsigned char)(0x80+(r%0x80));
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_strnicmp");
    if(!sys){printf("no _strnicmp\n");return 2;}
    static char A[600], B[600];
    unsigned long seed=0x45abcu;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<8; ++off){
            char* a=A+off; char* b=B+off;
            for(size_t i=0;i<len;++i){ unsigned char c=randch(&seed); a[i]=(char)(c?c:1); }
            a[len]=0;
            for(size_t i=0;i<len;++i){ unsigned char c=(unsigned char)a[i];
                if((c>=0x41&&c<=0x5A)||(c>=0x61&&c<=0x7A)){ seed=seed*1103515245u+12345u; if(seed&1) c^=0x20; }
                b[i]=(char)c; }
            b[len]=0;
            size_t ns[]={0,1,2,7,8,9,15,16,17,31,32,33, len?len-1:0, len, len+1, 300};
            for(int k=0;k<(int)(sizeof(ns)/sizeof(ns[0]));++k){
                size_t n=ns[k];
                chk(ref_strnicmp((unsigned char*)a,(unsigned char*)b,n), wia_strnicmp(a,b,n), sys(a,b,n), "case-eq", len, off, n);
                if(len){ size_t p=(seed>>3)%len; char save=b[p]; b[p]=(char)randch(&seed); if(!b[p])b[p]=1;
                    chk(ref_strnicmp((unsigned char*)a,(unsigned char*)b,n), wia_strnicmp(a,b,n), sys(a,b,n), "diff", len, off, n); b[p]=save; }
                if(len>1){ size_t p=1+((seed>>5)%len); if(p<=len){ char save=b[p-1]; b[p-1]=0;
                    chk(ref_strnicmp((unsigned char*)a,(unsigned char*)b,n), wia_strnicmp(a,b,n), sys(a,b,n), "short", len, off, n); b[p-1]=save; } }
            }
        }
    }
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* p1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* p2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(p1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(p2+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=140; tail++){
        char* t1=(char*)(p1+pg-tail), *s1=(char*)(p1+pg-400);
        char* t2=(char*)(p2+pg-tail), *s2=(char*)(p2+pg-400);
        for(char* q=s1;q<t1;++q)*q='A'; *t1=0;
        for(char* q=s2;q<t2;++q)*q='a'; *t2=0;
        chk(ref_strnicmp((unsigned char*)s1,(unsigned char*)s2,100000), wia_strnicmp(s1,s2,100000), sys(s1,s2,100000),"pg-eq",0,0,100000);
        *(t1-1)='C';
        chk(ref_strnicmp((unsigned char*)s1,(unsigned char*)s2,100000), wia_strnicmp(s1,s2,100000), sys(s1,s2,100000),"pg-diff",0,0,100000);
    }
    if(!failures) printf("CORRECTNESS: PASS (_strnicmp fuzz 0..300 x8 align x n{0..300}: case-eq/diff/short + >=0x80 no-fold + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
