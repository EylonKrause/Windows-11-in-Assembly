// changes/086-cryptstringtobinary-hexraw/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern int wia_s2bh(const char*, unsigned long, unsigned long, unsigned char*, unsigned long*, unsigned long*, unsigned long*);
int ref_s2bh(const char*, unsigned long, unsigned long, unsigned char*, unsigned long*, unsigned long*, unsigned long*);
void wia_hexrev_init(void);
typedef BOOL (WINAPI *encf)(const BYTE*,DWORD,DWORD,LPSTR,DWORD*);
typedef BOOL (WINAPI *decf)(LPCSTR,DWORD,DWORD,BYTE*,DWORD*,DWORD*,DWORD*);
static int fails=0;
int main(void){
    wia_hexrev_init();
    HMODULE c=LoadLibraryW(L"crypt32.dll");
    encf enc=(encf)GetProcAddress(c,"CryptBinaryToStringA");
    decf dec=(decf)GetProcAddress(c,"CryptStringToBinaryA");
    if(!dec){printf("no CryptStringToBinaryA\n");return 2;}
    unsigned long seed=0x86abcu;
    static unsigned char bin[3000]; static char hx[7000]; static unsigned char oO[3000],oS[3000],oR[3000];
    for(int t=0;t<90000;t++){ int n=1+(t%1500);
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; bin[i]=(unsigned char)(seed>>16); }
        int crlf=(t&1);
        DWORD cch=sizeof(hx); enc(bin,n,crlf?0x0cu:(0x0cu|0x40000000u),hx,&cch);
        DWORD qO=0,qS=0,qR=0;
        int rqO=wia_s2bh(hx,cch,0x0c,NULL,&qO,NULL,NULL); BOOL rqS=dec(hx,cch,0x0c,NULL,&qS,NULL,NULL); ref_s2bh(hx,cch,0x0c,NULL,&qR,NULL,NULL);
        if(!rqO||!rqS||qO!=qS||qO!=qR||qO!=(DWORD)n){ if(fails<8)printf("QUERY FAIL n=%d qO=%lu qS=%lu\n",n,qO,qS); if(++fails>12)goto done; continue; }
        DWORD cO=sizeof(oO),skO=9,flO=9, cS=sizeof(oS),skS=9,flS=9, cR=sizeof(oR),skR=9,flR=9;
        int rO=wia_s2bh(hx,cch,0x0c,oO,&cO,&skO,&flO); BOOL rS=dec(hx,cch,0x0c,oS,&cS,&skS,&flS); ref_s2bh(hx,cch,0x0c,oR,&cR,&skR,&flR);
        if(!rO||!rS||cO!=cS||skO!=skS||flO!=flS||cO!=cR||cO!=(DWORD)n||memcmp(oO,oS,cO)||memcmp(oO,oR,cO)||memcmp(oO,bin,n)){
            if(fails<8)printf("DEC FAIL n=%d cO=%lu cS=%lu sk=%lu/%lu fl=%lu/%lu\n",n,cO,cS,skO,skS,flO,flS); if(++fails>12)goto done; }
    }
done:;
    DWORD cb=100,sk,fl;
    if(wia_s2bh("AB*D",4,0x0cu,oO,&cb,&sk,&fl)!=0){ printf("invalid not FALSE\n"); ++fails; }
    if(wia_s2bh("ABC",3,0x0cu,oO,&cb,&sk,&fl)!=0){ printf("odd not FALSE\n"); ++fails; }
    if(!fails) printf("CORRECTNESS: PASS (CryptStringToBinaryA HEXRAW decode + query, fuzz 90000 x n 1..1500, vs live crypt32 + oracle + roundtrip)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
