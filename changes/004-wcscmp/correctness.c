// changes/004-wcscmp/correctness.c
// wcscmp's return is only SIGN-significant by contract, so we compare signs of
// ours vs scalar reference vs live ucrtbase wcscmp. Includes a page-guard test.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern int wia_wcscmp(const wchar_t*, const wchar_t*);
int ref_wcscmp(const wchar_t*, const wchar_t*);
typedef int (__cdecl *fn)(const wchar_t*, const wchar_t*);
static int failures=0;
static int sgn(int x){ return (x>0)-(x<0); }
static void check(int r,int o,int y,const char* what,size_t len,int off,int pos){
    if(sgn(o)!=sgn(r) || sgn(y)!=sgn(r)){
        printf("FAIL [%s] len=%zu off=%d pos=%d: ref=%d(%d) ours=%d(%d) sys=%d(%d)\n",
               what,len,off,pos,r,sgn(r),o,sgn(o),y,sgn(y)); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"wcscmp");
    if(!sys){printf("no wcscmp\n");return 2;}
    static wchar_t a[600], b[600];
    unsigned long seed=0x1357u;
    for(size_t len=0; len<=280; ++len){
        for(int off=0; off<8; ++off){
            wchar_t* s1=a+off; wchar_t* s2=b+off;
            for(size_t i=0;i<len;++i){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); s1[i]=s2[i]=(c?c:3); }
            s1[len]=0; s2[len]=0;
            check(ref_wcscmp(s1,s2), wia_wcscmp(s1,s2), sys(s1,s2), "equal", len, off, -1);
            // differ at each position (change s2)
            for(size_t pos=0; pos<len; pos += (len>40?5:1)){
                wchar_t save=s2[pos];
                s2[pos]=(wchar_t)(save+1?save+1:5);            // greater
                check(ref_wcscmp(s1,s2), wia_wcscmp(s1,s2), sys(s1,s2), "gt", len, off,(int)pos);
                s2[pos]=(wchar_t)(save-1?save-1:5);            // less
                check(ref_wcscmp(s1,s2), wia_wcscmp(s1,s2), sys(s1,s2), "lt", len, off,(int)pos);
                s2[pos]=save;
            }
            // prefix: truncate s2 earlier (s2 shorter -> s1 greater)
            if(len>0){ wchar_t sv=s2[len-1]; s2[len-1]=0;
                check(ref_wcscmp(s1,s2), wia_wcscmp(s1,s2), sys(s1,s2), "prefix", len, off,-1);
                s2[len-1]=sv; }
        }
    }
    // page-guard: both strings' terminators sit right before a NOACCESS page; equal -> must stop safely.
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* B1=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char* B2=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(B1+pg,pg,PAGE_NOACCESS,&old); VirtualProtect(B2+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=2; tail<=80; tail+=2){
        wchar_t* t1=(wchar_t*)(B1+pg-tail); wchar_t* t2=(wchar_t*)(B2+pg-tail);
        wchar_t* s1=(wchar_t*)(B1+pg-256); wchar_t* s2=(wchar_t*)(B2+pg-256);
        for(wchar_t* p=s1;p<t1;++p)*p=L'A'; *t1=0;
        for(wchar_t* p=s2;p<t2;++p)*p=L'A'; *t2=0;
        check(ref_wcscmp(s1,s2), wia_wcscmp(s1,s2), sys(s1,s2), "pg-equal",0,0,tail);
        // make them differ in the last wchar before the guard
        if(t1> s1){ *(t1-1)=L'B';
            check(ref_wcscmp(s1,s2), wia_wcscmp(s1,s2), sys(s1,s2), "pg-diff",0,0,tail); *(t1-1)=L'A'; }
    }
    if(!failures) printf("CORRECTNESS: PASS (equal/gt/lt/prefix fuzz + page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
