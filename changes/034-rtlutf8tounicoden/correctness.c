#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_u82u(wchar_t*, ULONG, PULONG, const void*, ULONG);
NTSTATUS ref_u82u(unsigned short*, unsigned long, unsigned long*, const unsigned char*, unsigned long);
typedef NTSTATUS (WINAPI *fn)(wchar_t*, ULONG, PULONG, const void*, ULONG);
static int failures=0;
static void one(fn sys, const unsigned char* s, int n, ULONG dbytes){
    wchar_t d1[900], d2[900]; ULONG l1=0,l2=0,lr=0; unsigned short dr[900];
    NTSTATUS s1=sys(d1,dbytes,&l1,s,n);
    NTSTATUS s2=wia_u82u(d2,dbytes,&l2,s,n);
    NTSTATUS sr=ref_u82u(dr,dbytes,&lr,s,n);
    int bad=(s1!=s2)||(s2!=sr)||(l1!=l2)||(l2!=lr);
    ULONG cmp=l1<dbytes?l1:dbytes;
    for(ULONG b=0;b<cmp && !bad;b++) if(((unsigned char*)d1)[b]!=((unsigned char*)d2)[b]) bad=1;
    if(bad){ printf("FAIL n=%d db=%lu: ntdll st=%lx l=%lu ours st=%lx l=%lu ref st=%lx l=%lu\n",n,dbytes,s1,l1,s2,l2,sr,lr); ++failures; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlUTF8ToUnicodeN");
    static unsigned char src[400]; unsigned long seed=1;
    for(int t=0;t<200000 && failures<10;t++){
        int n=t%200;
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; unsigned r=seed>>8; int p=r%10;
            if(p<5) src[i]=r%0x80; else if(p<7) src[i]=0xC0+(r%0x40);
            else if(p<8) src[i]=0xE0+(r%0x10); else if(p<9) src[i]=0xF0+(r%8); else src[i]=0x80+(r%0x40); }
        one(sys,src,n,1800);
        if(t%4==0 && n>0) one(sys,src,n,(ULONG)(n));   // small dst -> overflow
    }
    if(!failures) printf("CORRECTNESS: PASS (UTF-8 decode fuzz 200000: valid + all malformed classes + overflow, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
