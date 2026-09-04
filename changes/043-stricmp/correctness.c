// changes/043-stricmp/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern int wia_stricmp(const char*, const char*);
int ref_stricmp(const unsigned char*, const unsigned char*);
typedef int (__cdecl *fn)(const char*, const char*);
static int failures=0;
static int sgn(int x){ return (x>0)-(x<0); }
static void chk(int r,int o,int y,const char* what,size_t len,int off){
    if(sgn(o)!=sgn(r)||sgn(y)!=sgn(r)){ printf("FAIL [%s] len=%zu off=%d: ref=%d ours=%d sys=%d\n",what,len,off,r,o,y); ++failures; }
}
static unsigned char randch(unsigned long* s){
    *s=*s*1103515245u+12345u; unsigned r=*s>>8; int p=r%10;
    if(p<4) return (unsigned char)(0x41+(r%26));
    if(p<8) return (unsigned char)(0x61+(r%26));
    if(p<9) return (unsigned char)(0x20+(r%0x40));
    return (unsigned char)(0x80+(r%0x80));   // >=0x80: must NOT fold
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_stricmp");
    if(!sys){printf("no _stricmp\n");return 2;}
    static char A[600], B[600];
    unsigned long seed=0x43abcu;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<8; ++off){
            char* a=A+off; char* b=B+off;
            for(size_t i=0;i<len;++i){ unsigned char c=randch(&seed); a[i]=(char)(c?c:1); }
            a[len]=0;
            for(size_t i=0;i<len;++i){ unsigned char c=(unsigned char)a[i];
                if((c>=0x41&&c<=0x5A)||(c>=0x61&&c<=0x7A)){ seed=seed*1103515245u+12345u; if(seed&1) c^=0x20; }
                b[i]=(char)c; }
            b[len]=0;
            chk(ref_stricmp((unsigned char*)a,(unsigned char*)b), wia_stricmp(a,b), sys(a,b), "case-eq", len, off);
            if(len){ size_t p=(seed>>3)%len; char save=b[p]; b[p]=(char)randch(&seed); if(!b[p])b[p]=1;
                chk(ref_stricmp((unsigned char*)a,(unsigned char*)b), wia_stricmp(a,b), sys(a,b), "diff", len, off); b[p]=save; }
            if(len>1){ size_t p=1+((seed>>5)%len); if(p<=len){ char save=b[p-1]; b[p-1]=0;
                chk(ref_stricmp((unsigned char*)a,(unsigned char*)b), wia_stricmp(a,b), sys(a,b), "short", len, off); b[p-1]=save; } }
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
        chk(ref_stricmp((unsigned char*)s1,(unsigned char*)s2), wia_stricmp(s1,s2), sys(s1,s2),"pg-eq",0,tail);
        *(t1-1)='C';
        chk(ref_stricmp((unsigned char*)s1,(unsigned char*)s2), wia_stricmp(s1,s2), sys(s1,s2),"pg-diff",0,tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (_stricmp fuzz 0..300 x8 align: case-eq/diff/short + >=0x80 no-fold + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
