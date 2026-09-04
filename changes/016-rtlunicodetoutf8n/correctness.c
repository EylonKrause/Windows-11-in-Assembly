#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_u2u8(void*, ULONG, PULONG, const wchar_t*, ULONG);
NTSTATUS ref_u2u8(unsigned char*, unsigned long, unsigned long*, const unsigned short*, unsigned long);
typedef NTSTATUS (WINAPI *fn)(void*, ULONG, PULONG, const wchar_t*, ULONG);
static int failures=0;

static int one(fn sys, const wchar_t* src, int n, ULONG dstMax){
    unsigned char d1[3000], d2[3000], dr[3000];
    ULONG l1=0,l2=0,lr=0;
    NTSTATUS s1=sys(d1,dstMax,&l1,src,n*2);
    NTSTATUS s2=wia_u2u8(d2,dstMax,&l2,src,n*2);
    NTSTATUS sr=ref_u2u8(dr,dstMax,&lr,(const unsigned short*)src,n*2);
    int bad=(s1!=s2)||(s2!=sr)||(l1!=l2)||(l2!=lr);
    ULONG cmp = l1<dstMax? l1: dstMax;   // compare only bytes that were written
    for(ULONG i=0;i<cmp && !bad;i++) if(d1[i]!=d2[i]||d2[i]!=dr[i]) bad=1;
    if(bad){ printf("FAIL n=%d dstMax=%lu: ntdll st=%lx len=%lu | ours st=%lx len=%lu | ref st=%lx len=%lu\n",
                    n,dstMax,s1,l1,s2,l2,sr,lr); ++failures; }
    return bad;
}

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlUnicodeToUTF8N");
    static wchar_t src[400]; unsigned long seed=1;
    for(int t=0;t<80000 && failures<10;t++){
        int n=t%180;
        for(int i=0;i<n;i++){
            seed=seed*1103515245u+12345u; unsigned r=(seed>>8); int pick=r%10;
            if(pick<5) src[i]=(wchar_t)(r%0x80);
            else if(pick<7) src[i]=(wchar_t)(0x80+(r%0x780));
            else if(pick<9) src[i]=(wchar_t)(0x800+(r%0xF800));
            else src[i]=(wchar_t)(0xD800+(r%0x800));
        }
        one(sys, src, n, 3000);                 // fits
        if(t%4==0 && n>0) one(sys, src, n, (ULONG)(n));  // small buffer -> overflow path
    }
    if(!failures) printf("CORRECTNESS: PASS (UTF-8 encode fuzz 80000: ASCII/2/3/4-byte + surrogates + overflow, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
