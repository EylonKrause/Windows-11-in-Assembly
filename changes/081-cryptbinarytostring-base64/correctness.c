// changes/081-cryptbinarytostring-base64/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern int wia_b2s(const unsigned char*, unsigned long, unsigned long, char*, unsigned long*);
int ref_b2s(const unsigned char*, unsigned long, unsigned long, char*, unsigned long*);
typedef BOOL (WINAPI *fn)(const BYTE*,DWORD,DWORD,LPSTR,DWORD*);
static int fails=0;
int main(void){
    HMODULE c=LoadLibraryW(L"crypt32.dll"); fn sys=(fn)GetProcAddress(c,"CryptBinaryToStringA");
    if(!sys){printf("no CryptBinaryToStringA\n");return 2;}
    unsigned long seed=0x81abcu;
    static unsigned char bin[4096]; static char oO[500000],oS[500000],oR[500000];
    unsigned long flagset[2]={0x1u|0x40000000u, 0x1u};
    for(int t=0;t<80000;t++){ int n=1+(t%1600);
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; bin[i]=(unsigned char)(seed>>16); }
        for(int fi=0; fi<2; ++fi){ unsigned long flags=flagset[fi];
            DWORD qO=0,qS=0,qR=0;
            int rqO=wia_b2s(bin,n,flags,NULL,&qO); BOOL rqS=sys(bin,n,flags,NULL,&qS); ref_b2s(bin,n,flags,NULL,&qR);
            if(!rqO||!rqS||qO!=qS||qO!=qR){ if(fails<8)printf("QUERY FAIL n=%d fi=%d qO=%lu qS=%lu qR=%lu\n",n,fi,qO,qS,qR); if(++fails>12)goto done; continue; }
            DWORD cO=qO,cS=qS,cR=qR; memset(oO,0xCC,qO+8); memset(oS,0xCC,qS+8); memset(oR,0xCC,qR+8);
            int rO=wia_b2s(bin,n,flags,oO,&cO); BOOL rS=sys(bin,n,flags,oS,&cS); ref_b2s(bin,n,flags,oR,&cR);
            if(!rO||!rS||cO!=cS||cO!=cR||strcmp(oO,oS)||strcmp(oO,oR)){
                if(fails<8)printf("ENC FAIL n=%d fi=%d cO=%lu cS=%lu\n  O=[%.48s]\n  S=[%.48s]\n",n,fi,cO,cS,oO,oS);
                if(++fails>12)goto done; }
        }
    }
done:;
    DWORD z=100; if(wia_b2s(bin,0,0x1u,oO,&z)!=0){ printf("n=0 not FALSE\n"); ++fails; }
    if(!fails) printf("CORRECTNESS: PASS (CryptBinaryToStringA BASE64 NOCRLF+CRLF + query, fuzz 80000 x n 1..1600, vs live crypt32 + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
