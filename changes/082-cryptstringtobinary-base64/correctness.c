// changes/082-cryptstringtobinary-base64/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern int wia_s2b(const char*, unsigned long, unsigned long, unsigned char*, unsigned long*, unsigned long*, unsigned long*);
int ref_s2b(const char*, unsigned long, unsigned long, unsigned char*, unsigned long*, unsigned long*, unsigned long*);
void wia_b64rev_init(void);
typedef BOOL (WINAPI *encf)(const BYTE*,DWORD,DWORD,LPSTR,DWORD*);
typedef BOOL (WINAPI *decf)(LPCSTR,DWORD,DWORD,BYTE*,DWORD*,DWORD*,DWORD*);
static int fails=0;
int main(void){
    wia_b64rev_init();
    HMODULE c=LoadLibraryW(L"crypt32.dll");
    encf enc=(encf)GetProcAddress(c,"CryptBinaryToStringA");
    decf dec=(decf)GetProcAddress(c,"CryptStringToBinaryA");
    if(!dec){printf("no CryptStringToBinaryA\n");return 2;}
    unsigned long seed=0x82abcu;
    static unsigned char bin[3000]; static char b64[8000]; static unsigned char oO[3000],oS[3000],oR[3000];
    unsigned long flagset[2]={0x1u|0x40000000u,0x1u};
    for(int t=0;t<80000;t++){ int n=1+(t%1500);
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; bin[i]=(unsigned char)(seed>>16); }
        for(int fi=0; fi<2; ++fi){ unsigned long enf=flagset[fi];
            DWORD cch=sizeof(b64); enc(bin,n,enf,b64,&cch);
            DWORD qO=0,qS=0,qR=0;
            int rqO=wia_s2b(b64,cch,0x1,NULL,&qO,NULL,NULL); BOOL rqS=dec(b64,cch,0x1,NULL,&qS,NULL,NULL); ref_s2b(b64,cch,0x1,NULL,&qR,NULL,NULL);
            if(!rqO||!rqS||qO!=qS||qO!=qR||qO!=(DWORD)n){ if(fails<8)printf("QUERY FAIL n=%d qO=%lu qS=%lu qR=%lu\n",n,qO,qS,qR); if(++fails>12)goto done; continue; }
            DWORD cO=sizeof(oO),skO=9,flO=9, cS=sizeof(oS),skS=9,flS=9, cR=sizeof(oR),skR=9,flR=9;
            int rO=wia_s2b(b64,cch,0x1,oO,&cO,&skO,&flO); BOOL rS=dec(b64,cch,0x1,oS,&cS,&skS,&flS); ref_s2b(b64,cch,0x1,oR,&cR,&skR,&flR);
            if(!rO||!rS||cO!=cS||skO!=skS||flO!=flS||cO!=cR||cO!=(DWORD)n||
               memcmp(oO,oS,cO)||memcmp(oO,oR,cO)||memcmp(oO,bin,n)){
                if(fails<8)printf("DEC FAIL n=%d fi=%d cO=%lu cS=%lu sk=%lu/%lu fl=%lu/%lu\n",n,fi,cO,cS,skO,skS,flO,flS);
                if(++fails>12)goto done; }
        }
    }
done:;
    DWORD cb=100,sk,fl; if(wia_s2b("AQ*D",4,0x1u,oO,&cb,&sk,&fl)!=0){ printf("invalid('AQ*D') not FALSE\n"); ++fails; }
    // --- regression: cchString==0 (NUL-terminated) path, vs live crypt32 ---
    {
        struct { const char* s; const char* tag; } T[] = {
            {"TWFu","cch0-Man"}, {"TWFu\r\n","cch0-crlf"}, {"SGVsbG8=","cch0-pad"},
            {"","cch0-empty"}, {"AQ*D","cch0-invalid"},
        };
        for(int i=0;i<(int)(sizeof(T)/sizeof(T[0]));i++){
            unsigned char b1[64],b2[64]; DWORD c1=sizeof(b1),s1=9,f1=9,c2=sizeof(b2),s2=9,f2=9;
            memset(b1,0x11,sizeof(b1)); memset(b2,0x22,sizeof(b2));
            int r1=wia_s2b(T[i].s,0,0x1u,b1,&c1,&s1,&f1);
            BOOL r2=dec(T[i].s,0,0x1u,b2,&c2,&s2,&f2);
            if((!!r1)!=(!!r2) || (r2 && (c1!=c2||s1!=s2||f1!=f2||memcmp(b1,b2,c1)))){
                printf("REG FAIL [%s] ours{r=%d c=%lu s=%lu f=%lx} sys{r=%d c=%lu s=%lu f=%lx}\n",
                       T[i].tag,r1,c1,s1,f1,r2,c2,s2,f2); ++fails;
            }
        }
    }
    if(!fails) printf("CORRECTNESS: PASS (CryptStringToBinaryA base64 decode NOCRLF+CRLF+query, fuzz 80000 x n 1..1500, vs live crypt32 + oracle + roundtrip)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
