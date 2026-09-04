#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
typedef LONG NTSTATUS;
extern NTSTATUS wia_a2u(USTR*, const ASTR*, unsigned char);
long ref_a2u(USTR*, const ASTR*, int);
void wia_a2umap_init(void);
typedef NTSTATUS (WINAPI *fn)(USTR*,const ASTR*,BOOLEAN);
static int failures=0;
int main(void){
    wia_a2umap_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlAnsiStringToUnicodeString");
    static char src[300]; static wchar_t d1[300], d2[300], dr[300]; unsigned long seed=1;
    for(int n=0;n<=280;++n){
        int rng=(n%3)?128:256;
        for(int i=0;i<n;i++){seed=seed*1103515245u+12345u; src[i]=(char)((seed>>16)%rng);}
        ASTR us={(unsigned short)n,(unsigned short)n,src};
        USTR u1={0,600,d1}, u2={0,600,d2}, ur={0,600,dr};
        long yo=sys(&u1,&us,FALSE), oo=wia_a2u(&u2,&us,0), ro=ref_a2u((void*)&ur,(void*)&us,0);
        int bad=(yo!=oo)||(oo!=ro)||(u1.Length!=u2.Length)||(u2.Length!=ur.Length);
        for(int i=0;i<n && !bad;i++) if(d2[i]!=d1[i]||d2[i]!=dr[i]) bad=1;
        if(bad){ printf("FAIL n=%d: ntdll st=%lx len=%u ours st=%lx len=%u\n",n,yo,u1.Length,oo,u2.Length); if(++failures>8)return 1; }
    }
    { ASTR us={40,40,src}; USTR u2={0,20,d2}; if(wia_a2u(&u2,&us,0)!=(long)0x80000005){printf("FAIL overflow\n");++failures;} }
    if(!failures) printf("CORRECTNESS: PASS (ANSI->UTF-16 n=0..280, ASCII+highbytes + overflow, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
