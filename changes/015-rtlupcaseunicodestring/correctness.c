#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef LONG NTSTATUS;
extern NTSTATUS wia_upcasestr(USTR*, const USTR*, unsigned char);
long ref_upcasestr(USTR*, const USTR*, int);
void wia_upcase_init(void);
typedef NTSTATUS (WINAPI *fn)(USTR*,const USTR*,BOOLEAN);
static int failures=0;
int main(void){
    wia_upcase_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlUpcaseUnicodeString");
    static wchar_t src[300], d1[300], d2[300], dr[300]; unsigned long seed=1;
    for(int n=0;n<=280;++n){
        int rng=(n%4)?0x80:0x600;
        for(int i=0;i<n;i++){seed=seed*1103515245u+12345u; src[i]=(wchar_t)((seed>>16)%rng+1);}
        USTR us={(unsigned short)(n*2),(unsigned short)(n*2),src};
        USTR u1={0,(unsigned short)(n*2),d1}, u2={0,(unsigned short)(n*2),d2}, ur={0,(unsigned short)(n*2),dr};
        long yo=sys(&u1,&us,FALSE), oo=wia_upcasestr(&u2,&us,0), ro=ref_upcasestr(&ur,&us,0);
        int bad=(yo!=oo)||(oo!=ro)||(u1.Length!=u2.Length)||(u2.Length!=ur.Length);
        for(int i=0;i<n && !bad;i++) if(d2[i]!=d1[i]||d2[i]!=dr[i]) bad=1;
        if(bad){ printf("FAIL n=%d: ntdll st=%lx len=%u ours st=%lx len=%u\n",n,yo,u1.Length,oo,u2.Length); if(++failures>8)return 1; }
    }
    { USTR us={40,40,src}; USTR u2={0,20,d2}; if(wia_upcasestr(&u2,&us,0)!=(long)0x80000005){printf("FAIL overflow status\n");++failures;} }
    if(!failures) printf("CORRECTNESS: PASS (upcase transform n=0..280, ASCII+nonASCII + overflow, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
