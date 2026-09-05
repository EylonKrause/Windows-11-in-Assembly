// changes/093-cryptbinarytostringw-base64header/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int wia_b2sh64w(const BYTE*, DWORD, DWORD, WCHAR*, DWORD*);
DWORD ref_b2s_hdr(const BYTE*, DWORD, DWORD, char*);   // narrow oracle
typedef BOOL (WINAPI *fn)(const BYTE*,DWORD,DWORD,LPWSTR,DWORD*);
static fn sys;
static int failures=0;
static void chk(const BYTE* in, DWORD n, DWORD fl){
    DWORD sq=0, oq=0;
    BOOL sr=sys(in,n,fl,NULL,&sq);
    int  orr=wia_b2sh64w(in,n,fl,NULL,&oq);
    DWORD L=ref_b2s_hdr(in,n,fl,NULL);
    if(!sr||!orr){ printf("FAIL query ret fl=%lx n=%lu\n",fl,n); ++failures; return; }
    if(sq!=oq || sq!=L+1){ printf("FAIL query len fl=%lx n=%lu sys=%lu ours=%lu L+1=%lu\n",fl,n,sq,oq,L+1); ++failures; return; }
    WCHAR* so=(WCHAR*)malloc((sq+8)*2); WCHAR* oo=(WCHAR*)malloc((sq+8)*2);
    memset(so,0xCC,(sq+8)*2); memset(oo,0xDD,(sq+8)*2);
    DWORD sc=sq, oc=sq;
    BOOL sr2=sys(in,n,fl,so,&sc);
    int  or2=wia_b2sh64w(in,n,fl,oo,&oc);
    if(!sr2||!or2){ printf("FAIL conv ret fl=%lx n=%lu\n",fl,n); ++failures; }
    else if(sc!=oc || sc!=L){ printf("FAIL conv len fl=%lx n=%lu sys=%lu ours=%lu L=%lu\n",fl,n,sc,oc,L); ++failures; }
    else if(memcmp(so,oo,sc*2)!=0){ printf("FAIL conv bytes fl=%lx n=%lu\n",fl,n); ++failures;
        for(DWORD i=0;i<sc;i++) if(so[i]!=oo[i]){ printf("  at %lu sys=%04x ours=%04x\n",i,so[i],oo[i]); if(i>4)break; } }
    else if(oo[oc]!=0){ printf("FAIL wNUL fl=%lx n=%lu\n",fl,n); ++failures; }
    free(so); free(oo);
}
int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll");
    sys=(fn)GetProcAddress(h,"CryptBinaryToStringW");
    if(!sys){ printf("no CryptBinaryToStringW\n"); return 2; }
    DWORD N=40000; BYTE* buf=(BYTE*)malloc(N);
    unsigned s=0xF00Du; for(DWORD i=0;i<N;i++){ s=s*1103515245u+12345u; buf[i]=(BYTE)(s>>16); }
    DWORD flags[3]={0x0,0x3,0x9};
    for(int f=0;f<3;f++){ for(DWORD n=1;n<=1500;n++) chk(buf,n,flags[f]); chk(buf,N,flags[f]); }
    if(!failures) printf("CORRECTNESS: PASS (wide BASE64HEADER/REQUESTHEADER/X509CRLHEADER, query+convert, n=1..1500 + 40KB, vs live crypt32 + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
