// changes/107-cryptstringtobinaryw-base64any/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int wia_s2bw_any(const WCHAR*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
int ref_any_decode(const char*, BYTE*, DWORD*, DWORD*, DWORD*);   // narrow oracle
typedef BOOL (WINAPI *fn)(LPCWSTR,DWORD,DWORD,BYTE*,DWORD*,DWORD*,DWORD*);
static fn sys;
static int failures=0;
static char* narrow(const WCHAR* w){ size_t n=wcslen(w); char* s=(char*)malloc(n+1); for(size_t i=0;i<n;i++)s[i]=(char)w[i]; s[n]=0; return s; }
static void chk(const WCHAR* s, const char* what){
    DWORD scb=0,ssk=0,sff=0; BOOL sr=sys(s,0,0x6,NULL,&scb,&ssk,&sff);
    DWORD ocb=0,osk=0,off=0;  int  orr=wia_s2bw_any(s,0,0x6,NULL,&ocb,&osk,&off);
    if((!!sr)!=(!!orr)){ printf("FAIL [%s] query ret sys=%d ours=%d\n",what,sr,orr); ++failures; return; }
    if(!sr) return;
    BYTE* so=(BYTE*)malloc(scb+16); BYTE* oo=(BYTE*)malloc(scb+16);
    DWORD scb2=scb,ssk2=0,sff2=0; BOOL sr2=sys(s,0,0x6,so,&scb2,&ssk2,&sff2);
    DWORD ocb2=scb,osk2=0,off2=0; int  or2=wia_s2bw_any(s,0,0x6,oo,&ocb2,&osk2,&off2);
    if((!!sr2)!=(!!or2)||scb2!=ocb2||ssk2!=osk2||sff2!=off2||memcmp(so,oo,scb2)){
        printf("FAIL [%s] sys{ok=%d cb=%lu sk=%lu ff=%lx} ours{ok=%d cb=%lu sk=%lu ff=%lx}\n",what,sr2,scb2,ssk2,sff2,or2,ocb2,osk2,off2);++failures;
    }
    char* ns=narrow(s); BYTE* ro=(BYTE*)malloc(scb+16); DWORD rcb=0,rsk=0,rff=0; ref_any_decode(ns,ro,&rcb,&rsk,&rff);
    if(rcb!=scb2||rsk!=ssk2||rff!=sff2||memcmp(ro,so,rcb)){ printf("FAIL [%s] oracle mismatch\n",what); ++failures; }
    free(so);free(oo);free(ro);free(ns);
}
int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll"); sys=(fn)GetProcAddress(h,"CryptStringToBinaryW");
    if(!sys){printf("no CryptStringToBinaryW\n");return 2;}
    for(int n=1;n<=500;n++){
        BYTE* d=(BYTE*)malloc(n); for(int i=0;i<n;i++)d[i]=(BYTE)(i*5+n*3);
        DWORD c=0;CryptBinaryToStringW(d,n,0x0,NULL,&c);WCHAR*pem=(WCHAR*)malloc((c+40)*2);DWORD c2=c;CryptBinaryToStringW(d,n,0x0,pem,&c2);
        chk(pem,"pem");
        DWORD e=0;CryptBinaryToStringW(d,n,0x1,NULL,&e);WCHAR*b64=(WCHAR*)malloc((e+40)*2);DWORD e2=e;CryptBinaryToStringW(d,n,0x1,b64,&e2);
        chk(b64,"plainb64");
        free(pem);free(b64);free(d);
    }
    if(!failures) printf("CORRECTNESS: PASS (wide BASE64_ANY decode: PEM(ff=0)+plain base64(ff=1), n=1..500, query+convert, vs live + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
