#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_u2uoem(char*, ULONG, PULONG, const wchar_t*, ULONG);
NTSTATUS ref_u2uoem(unsigned char*, unsigned long, unsigned long*, const unsigned short*, unsigned long);
void wia_upoemmap_init(void);
typedef NTSTATUS (WINAPI *fn)(char*, ULONG, PULONG, const wchar_t*, ULONG);
static int failures=0;
static int one(fn sys,const wchar_t*src,int n,ULONG mx){
    char d1[600],d2[600],dr[600]; ULONG l1=0,l2=0,lr=0;
    NTSTATUS s1=sys(d1,mx,&l1,src,n*2), s2=wia_u2uoem(d2,mx,&l2,src,n*2);
    NTSTATUS sr=ref_u2uoem((void*)dr,mx,&lr,(const unsigned short*)src,n*2);
    int bad=(s1!=s2)||(s2!=sr)||(l1!=l2)||(l2!=lr);
    for(ULONG i=0;i<l1 && !bad;i++) if(d1[i]!=d2[i]||d2[i]!=dr[i]) bad=1;
    if(bad){ printf("FAIL n=%d mx=%lu: ntdll st=%lx l=%lu ours st=%lx l=%lu\n",n,mx,s1,l1,s2,l2); ++failures; }
    return bad;
}
int main(void){
    wia_upoemmap_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlUpcaseUnicodeToOemN");
    static wchar_t src[300]; unsigned long seed=1;
    for(int n=0;n<=280 && failures<8;++n){
        int rng=(n%4)?0x80:0x600;
        for(int i=0;i<n;i++){seed=seed*1103515245u+12345u; src[i]=(wchar_t)((seed>>16)%rng+1);}
        one(sys,src,n,600);                 // fits
        if(n>0) one(sys,src,n,(ULONG)(n/2));// overflow
    }
    if(!failures) printf("CORRECTNESS: PASS (upcase UTF-16->OEM N n=0..280, ASCII+nonASCII + overflow, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
