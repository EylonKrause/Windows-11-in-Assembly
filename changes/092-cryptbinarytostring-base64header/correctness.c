// changes/092-cryptbinarytostring-base64header/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int wia_b2sh64(const BYTE*, DWORD, DWORD, char*, DWORD*);
DWORD ref_b2s_hdr(const BYTE*, DWORD, DWORD, char*);
typedef BOOL (WINAPI *fn)(const BYTE*,DWORD,DWORD,LPSTR,DWORD*);
static fn sys;
static int failures=0;
static void chk(const BYTE* in, DWORD n, DWORD fl){
    DWORD sq=0, oq=0;
    BOOL sr=sys(in,n,fl,NULL,&sq);
    int  orr=wia_b2sh64(in,n,fl,NULL,&oq);
    DWORD refl=ref_b2s_hdr(in,n,fl,NULL);
    if(!sr||!orr){ printf("FAIL query ret fl=%lx n=%lu\n",fl,n); ++failures; return; }
    if(sq!=oq || sq!=refl+1){ printf("FAIL query len fl=%lx n=%lu sys=%lu ours=%lu ref+1=%lu\n",fl,n,sq,oq,refl+1); ++failures; return; }
    char* so=(char*)malloc(sq+8); char* oo=(char*)malloc(sq+8);
    memset(so,0xCC,sq+8); memset(oo,0xDD,sq+8);
    DWORD sc=sq, oc=sq;
    BOOL sr2=sys(in,n,fl,so,&sc);
    int  or2=wia_b2sh64(in,n,fl,oo,&oc);
    if(!sr2||!or2){ printf("FAIL conv ret fl=%lx n=%lu\n",fl,n); ++failures; }
    else if(sc!=oc || sc!=refl){ printf("FAIL conv len fl=%lx n=%lu sys=%lu ours=%lu ref=%lu\n",fl,n,sc,oc,refl); ++failures; }
    else if(memcmp(so,oo,sc)!=0){ printf("FAIL conv bytes fl=%lx n=%lu\n",fl,n); ++failures;
        for(DWORD i=0;i<sc;i++) if(so[i]!=oo[i]){ printf("  at %lu sys=%02x ours=%02x\n",i,(BYTE)so[i],(BYTE)oo[i]); if(i>4)break; } }
    else if((BYTE)oo[oc]!=0){ printf("FAIL NUL fl=%lx n=%lu\n",fl,n); ++failures; }
    free(so); free(oo);
}
int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll");
    sys=(fn)GetProcAddress(h,"CryptBinaryToStringA");
    if(!sys){ printf("no CryptBinaryToStringA\n"); return 2; }
    DWORD N=40000; BYTE* buf=(BYTE*)malloc(N);
    unsigned s=0x5EEDu; for(DWORD i=0;i<N;i++){ s=s*1103515245u+12345u; buf[i]=(BYTE)(s>>16); }
    DWORD flags[3]={0x0,0x3,0x9};
    for(int f=0;f<3;f++){
        for(DWORD n=1;n<=1500;n++) chk(buf,n,flags[f]);
        chk(buf,N,flags[f]);
    }
    if(!failures) printf("CORRECTNESS: PASS (BASE64HEADER/REQUESTHEADER/X509CRLHEADER, query+convert, n=1..1500 + 40KB, vs live crypt32 + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
