#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_mb2u(wchar_t*, ULONG, PULONG, const char*, ULONG);
NTSTATUS ref_mb2u(unsigned short*, unsigned long, unsigned long*, const unsigned char*, unsigned long);
void wia_a2umap_init(void);
typedef NTSTATUS (WINAPI *fn)(wchar_t*, ULONG, PULONG, const char*, ULONG);
static int failures=0;
static int one(fn sys,const char*src,int n,ULONG mx){
    wchar_t d1[600],d2[600],dr[600]; ULONG l1=0,l2=0,lr=0;
    NTSTATUS s1=sys(d1,mx,&l1,src,n), s2=wia_mb2u(d2,mx,&l2,src,n);
    NTSTATUS sr=ref_mb2u((void*)dr,mx,&lr,(const unsigned char*)src,n);
    int bad=(s1!=s2)||(s2!=sr)||(l1!=l2)||(l2!=lr);
    for(ULONG i=0;i<l1/2 && !bad;i++) if(d1[i]!=d2[i]||d2[i]!=dr[i]) bad=1;
    if(bad){ printf("FAIL n=%d mx=%lu: ntdll st=%lx l=%lu ours st=%lx l=%lu\n",n,mx,s1,l1,s2,l2); ++failures; }
    return bad;
}
int main(void){
    wia_a2umap_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlMultiByteToUnicodeN");
    static char src[300]; unsigned long seed=1;
    for(int n=0;n<=280 && failures<8;++n){
        int rng=(n%3)?128:256;
        for(int i=0;i<n;i++){seed=seed*1103515245u+12345u; src[i]=(char)((seed>>16)%rng);}
        one(sys,src,n,1200);              // fits
        if(n>0) one(sys,src,n,(ULONG)n);  // half room -> truncation
    }
    if(!failures) printf("CORRECTNESS: PASS (ANSI->UTF-16 N n=0..280, ASCII+highbytes + truncation, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
