// changes/084-cryptstringtobinaryw-base64/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern int wia_s2bw(const wchar_t*, unsigned long, unsigned long, unsigned char*, unsigned long*, unsigned long*, unsigned long*);
int ref_s2bw(const wchar_t*, unsigned long, unsigned long, unsigned char*, unsigned long*, unsigned long*, unsigned long*);
void wia_b64rev_init(void);
typedef BOOL (WINAPI *encf)(const BYTE*,DWORD,DWORD,LPWSTR,DWORD*);
typedef BOOL (WINAPI *decf)(LPCWSTR,DWORD,DWORD,BYTE*,DWORD*,DWORD*,DWORD*);
static int fails=0;
int main(void){
    wia_b64rev_init();
    HMODULE c=LoadLibraryW(L"crypt32.dll");
    encf enc=(encf)GetProcAddress(c,"CryptBinaryToStringW");
    decf dec=(decf)GetProcAddress(c,"CryptStringToBinaryW");
    if(!dec){printf("no CryptStringToBinaryW\n");return 2;}
    unsigned long seed=0x84abcu;
    static unsigned char bin[3000]; static wchar_t b64[8000]; static unsigned char oO[3000],oS[3000],oR[3000];
    unsigned long flagset[2]={0x1u|0x40000000u,0x1u};
    for(int t=0;t<70000;t++){ int n=1+(t%1500);
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; bin[i]=(unsigned char)(seed>>16); }
        for(int fi=0; fi<2; ++fi){ unsigned long enf=flagset[fi];
            DWORD cch=sizeof(b64)/2; enc(bin,n,enf,b64,&cch);
            DWORD qO=0,qS=0,qR=0;
            int rqO=wia_s2bw(b64,cch,0x1,NULL,&qO,NULL,NULL); BOOL rqS=dec(b64,cch,0x1,NULL,&qS,NULL,NULL); ref_s2bw(b64,cch,0x1,NULL,&qR,NULL,NULL);
            if(!rqO||!rqS||qO!=qS||qO!=qR||qO!=(DWORD)n){ if(fails<8)printf("QUERY FAIL n=%d qO=%lu qS=%lu\n",n,qO,qS); if(++fails>12)goto done; continue; }
            DWORD cO=sizeof(oO),skO=9,flO=9, cS=sizeof(oS),skS=9,flS=9, cR=sizeof(oR),skR=9,flR=9;
            int rO=wia_s2bw(b64,cch,0x1,oO,&cO,&skO,&flO); BOOL rS=dec(b64,cch,0x1,oS,&cS,&skS,&flS); ref_s2bw(b64,cch,0x1,oR,&cR,&skR,&flR);
            if(!rO||!rS||cO!=cS||skO!=skS||flO!=flS||cO!=cR||cO!=(DWORD)n||memcmp(oO,oS,cO)||memcmp(oO,oR,cO)||memcmp(oO,bin,n)){
                if(fails<8)printf("DEC FAIL n=%d fi=%d cO=%lu cS=%lu\n",n,fi,cO,cS); if(++fails>12)goto done; }
        }
    }
done:;
    DWORD cb=100,sk,fl; if(wia_s2bw(L"AQ*D",4,0x1u,oO,&cb,&sk,&fl)!=0){ printf("invalid not FALSE\n"); ++fails; }
    if(wia_s2bw(L"AQ\x0141""D",4,0x1u,oO,&cb,&sk,&fl)!=0){ printf("wide-char not FALSE\n"); ++fails; }
    if(!fails) printf("CORRECTNESS: PASS (CryptStringToBinaryW base64 decode NOCRLF+CRLF+query, fuzz 70000 x n 1..1500, vs live crypt32 + oracle + roundtrip)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
