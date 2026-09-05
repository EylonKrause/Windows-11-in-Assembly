// changes/105-cryptstringtobinaryw-base64header/correctness.c
// Bit-exact vs live crypt32!CryptStringToBinaryW (BASE64HEADER) AND the (narrowed) oracle:
// return, cbBinary, pdwSkip (in WCHARs), pdwFlags, decoded bytes; query + convert; canonical
// PEM plus leading/trailing garbage and multi-line bodies.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int wia_s2bw_pem(const WCHAR*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern void wia_b64rev_init(void);
int ref_pem_decode(const char*, BYTE*, DWORD*, DWORD*);   // narrow oracle
typedef BOOL (WINAPI *fn)(LPCWSTR,DWORD,DWORD,BYTE*,DWORD*,DWORD*,DWORD*);
static fn sys;
static int failures=0;
static char* narrow(const WCHAR* w){ // ASCII-narrow (PEM is ASCII)
    size_t n=wcslen(w); char* s=(char*)malloc(n+1); for(size_t i=0;i<n;i++) s[i]=(char)w[i]; s[n]=0; return s;
}
static void chk(const WCHAR* pem, const char* what){
    DWORD scb=0,ssk=0,sff=0; BOOL sr=sys(pem,0,0x0,NULL,&scb,&ssk,&sff);
    DWORD ocb=0,osk=0,off=0;  int  orr=wia_s2bw_pem(pem,0,0x0,NULL,&ocb,&osk,&off);
    if((!!sr)!=(!!orr)){ printf("FAIL [%s] query ret sys=%d ours=%d\n",what,sr,orr); ++failures; return; }
    if(!sr) return;
    BYTE* so=(BYTE*)malloc(scb+16); BYTE* oo=(BYTE*)malloc(scb+16);
    DWORD scb2=scb,ssk2=0,sff2=0; BOOL sr2=sys(pem,0,0x0,so,&scb2,&ssk2,&sff2);
    DWORD ocb2=scb,osk2=0,off2=0; int  or2=wia_s2bw_pem(pem,0,0x0,oo,&ocb2,&osk2,&off2);
    if((!!sr2)!=(!!or2) || scb2!=ocb2 || ssk2!=osk2 || sff2!=off2 || memcmp(so,oo,scb2)!=0){
        printf("FAIL [%s] sys{ok=%d cb=%lu sk=%lu ff=%lx} ours{ok=%d cb=%lu sk=%lu ff=%lx}\n",
            what,sr2,scb2,ssk2,sff2,or2,ocb2,osk2,off2); ++failures;
        for(DWORD i=0;i<scb2;i++) if(so[i]!=oo[i]){ printf("  byte@%lu sys=%02x ours=%02x\n",i,so[i],oo[i]); break; }
    }
    char* ns=narrow(pem); BYTE* ro=(BYTE*)malloc(scb+16); DWORD rcb=0,rsk=0; ref_pem_decode(ns,ro,&rcb,&rsk);
    if(rcb!=scb2 || rsk!=ssk2 || memcmp(ro,so,rcb)!=0){ printf("FAIL [%s] oracle mismatch (cb r=%lu s=%lu, sk r=%lu s=%lu)\n",what,rcb,scb2,rsk,ssk2); ++failures; }
    free(so);free(oo);free(ro);free(ns);
}
int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll");
    sys=(fn)GetProcAddress(h,"CryptStringToBinaryW");
    wia_b64rev_init();
    if(!sys){ printf("no CryptStringToBinaryW\n"); return 2; }
    for(int n=1;n<=500;n++){
        BYTE* d=(BYTE*)malloc(n); for(int i=0;i<n;i++) d[i]=(BYTE)(i*5+n*3);
        DWORD c=0; CryptBinaryToStringW(d,n,0x0,NULL,&c); WCHAR* pem=(WCHAR*)malloc((c+80)*2); DWORD c2=c;
        CryptBinaryToStringW(d,n,0x0,pem,&c2);
        chk(pem,"canonical");
        WCHAR* g=(WCHAR*)malloc((c+120)*2); wsprintfW(g,L"junk\r\nx\r\n%s",pem); chk(g,"leadgarbage"); free(g);
        free(pem); free(d);
    }
    chk(L"not a pem\r\njust text\r\n","noheader");
    // --- regression: empty (wide -> FALSE/87) + headerless, direct vs live crypt32 ---
    {
        const wchar_t* S[]={L"",L"TWFu",L"garbage!!"}; const char* G[]={"empty","headerless","garbage"};
        for(int i=0;i<3;i++){
            BYTE b1[64],b2[64]; DWORD c1=sizeof(b1),s1=9,f1=9,c2=sizeof(b2),s2=9,f2=9;
            memset(b1,0x11,sizeof(b1)); memset(b2,0x22,sizeof(b2));
            int r1=wia_s2bw_pem(S[i],0,0x0,b1,&c1,&s1,&f1);
            BOOL r2=sys(S[i],0,0x0,b2,&c2,&s2,&f2);
            if((!!r1)!=(!!r2)||(r2&&(c1!=c2||s1!=s2||f1!=f2||memcmp(b1,b2,c1)))){
                printf("REG FAIL [%s] ours{r=%d c=%lu} sys{r=%d c=%lu}\n",G[i],r1,c1,r2,c2); ++failures; }
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (wide BASE64HEADER decode: canonical + leadgarbage + multi-line + no-header, n=1..500, query+convert, vs live crypt32 + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
