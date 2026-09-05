// changes/083-cryptbinarytostringw-base64/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern int wia_b2sw(const unsigned char*, unsigned long, unsigned long, wchar_t*, unsigned long*);
int ref_b2sw(const unsigned char*, unsigned long, unsigned long, wchar_t*, unsigned long*);
typedef BOOL (WINAPI *fn)(const BYTE*,DWORD,DWORD,LPWSTR,DWORD*);
static int fails=0;
int main(void){
    HMODULE c=LoadLibraryW(L"crypt32.dll"); fn sys=(fn)GetProcAddress(c,"CryptBinaryToStringW");
    if(!sys){printf("no CryptBinaryToStringW\n");return 2;}
    unsigned long seed=0x83abcu;
    static unsigned char bin[4096]; static wchar_t oO[500000],oS[500000],oR[500000];
    unsigned long flagset[2]={0x1u|0x40000000u,0x1u};
    for(int t=0;t<70000;t++){ int n=1+(t%1600);
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; bin[i]=(unsigned char)(seed>>16); }
        for(int fi=0; fi<2; ++fi){ unsigned long flags=flagset[fi];
            DWORD qO=0,qS=0,qR=0;
            int rqO=wia_b2sw(bin,n,flags,NULL,&qO); BOOL rqS=sys(bin,n,flags,NULL,&qS); ref_b2sw(bin,n,flags,NULL,&qR);
            if(!rqO||!rqS||qO!=qS||qO!=qR){ if(fails<8)printf("QUERY FAIL n=%d fi=%d qO=%lu qS=%lu\n",n,fi,qO,qS); if(++fails>12)goto done; continue; }
            DWORD cO=qO,cS=qS,cR=qR; memset(oO,0xCC,(qO+8)*2); memset(oS,0xCC,(qS+8)*2); memset(oR,0xCC,(qR+8)*2);
            int rO=wia_b2sw(bin,n,flags,oO,&cO); BOOL rS=sys(bin,n,flags,oS,&cS); ref_b2sw(bin,n,flags,oR,&cR);
            if(!rO||!rS||cO!=cS||cO!=cR||wcscmp(oO,oS)||wcscmp(oO,oR)){
                if(fails<8)printf("ENC FAIL n=%d fi=%d cO=%lu cS=%lu\n  O=[%.40ls]\n  S=[%.40ls]\n",n,fi,cO,cS,oO,oS);
                if(++fails>12)goto done; }
        }
    }
done:;
    DWORD z=100; if(wia_b2sw(bin,0,0x1u,oO,&z)!=0){ printf("n=0 not FALSE\n"); ++fails; }
    if(!fails) printf("CORRECTNESS: PASS (CryptBinaryToStringW BASE64 NOCRLF+CRLF + query, fuzz 70000 x n 1..1600, vs live crypt32 + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
