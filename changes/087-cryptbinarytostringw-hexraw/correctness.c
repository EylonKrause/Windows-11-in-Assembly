// changes/087-cryptbinarytostringw-hexraw/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern int wia_b2shw(const unsigned char*, unsigned long, unsigned long, wchar_t*, unsigned long*);
int ref_b2shw(const unsigned char*, unsigned long, unsigned long, wchar_t*, unsigned long*);
typedef BOOL (WINAPI *fn)(const BYTE*,DWORD,DWORD,LPWSTR,DWORD*);
static int fails=0;
int main(void){
    HMODULE c=LoadLibraryW(L"crypt32.dll"); fn sys=(fn)GetProcAddress(c,"CryptBinaryToStringW");
    unsigned long seed=0x87abcu;
    static unsigned char bin[4096]; static wchar_t oO[12000],oS[12000],oR[12000];
    unsigned long flagset[2]={0x0cu,0x0cu|0x40000000u};
    for(int t=0;t<70000;t++){ int n=1+(t%2000);
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; bin[i]=(unsigned char)(seed>>16); }
        for(int fi=0; fi<2; ++fi){ unsigned long flags=flagset[fi];
            DWORD qO=0,qS=0,qR=0;
            int rqO=wia_b2shw(bin,n,flags,NULL,&qO); BOOL rqS=sys(bin,n,flags,NULL,&qS); ref_b2shw(bin,n,flags,NULL,&qR);
            if(!rqO||!rqS||qO!=qS||qO!=qR){ if(fails<8)printf("QUERY FAIL n=%d qO=%lu qS=%lu\n",n,qO,qS); if(++fails>12)goto done; continue; }
            DWORD cO=qO,cS=qS,cR=qR; memset(oO,0xCC,(qO+8)*2); memset(oS,0xCC,(qS+8)*2); memset(oR,0xCC,(qR+8)*2);
            int rO=wia_b2shw(bin,n,flags,oO,&cO); BOOL rS=sys(bin,n,flags,oS,&cS); ref_b2shw(bin,n,flags,oR,&cR);
            if(!rO||!rS||cO!=cS||cO!=cR||wcscmp(oO,oS)||wcscmp(oO,oR)){
                if(fails<8)printf("ENC FAIL n=%d fi=%d cO=%lu cS=%lu\n",n,fi,cO,cS); if(++fails>12)goto done; }
        }
    }
done:;
    DWORD z=100; if(wia_b2shw(bin,0,0x0cu,oO,&z)!=0){ printf("n=0 not FALSE\n"); ++fails; }
    if(!fails) printf("CORRECTNESS: PASS (CryptBinaryToStringW HEXRAW +/-NOCRLF + query, fuzz 70000 x n 1..2000, vs live crypt32 + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
