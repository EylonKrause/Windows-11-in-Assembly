// changes/106-cryptstringtobinary-base64any/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int wia_s2b_any(const char*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern void wia_b64rev_init(void);
int ref_any_decode(const char*, BYTE*, DWORD*, DWORD*, DWORD*);
typedef BOOL (WINAPI *fn)(LPCSTR,DWORD,DWORD,BYTE*,DWORD*,DWORD*,DWORD*);
static fn sys;
static int failures=0;
static void chk(const char* s, const char* what){
    DWORD scb=0,ssk=0,sff=0; BOOL sr=sys(s,0,0x6,NULL,&scb,&ssk,&sff);
    DWORD ocb=0,osk=0,off=0;  int  orr=wia_s2b_any(s,0,0x6,NULL,&ocb,&osk,&off);
    if((!!sr)!=(!!orr)){ printf("FAIL [%s] query ret sys=%d ours=%d\n",what,sr,orr); ++failures; return; }
    if(!sr) return;
    BYTE* so=(BYTE*)malloc(scb+16); BYTE* oo=(BYTE*)malloc(scb+16);
    DWORD scb2=scb,ssk2=0,sff2=0; BOOL sr2=sys(s,0,0x6,so,&scb2,&ssk2,&sff2);
    DWORD ocb2=scb,osk2=0,off2=0; int  or2=wia_s2b_any(s,0,0x6,oo,&ocb2,&osk2,&off2);
    if((!!sr2)!=(!!or2)||scb2!=ocb2||ssk2!=osk2||sff2!=off2||memcmp(so,oo,scb2)){
        printf("FAIL [%s] sys{ok=%d cb=%lu sk=%lu ff=%lx} ours{ok=%d cb=%lu sk=%lu ff=%lx}\n",what,sr2,scb2,ssk2,sff2,or2,ocb2,osk2,off2);++failures;
        for(DWORD i=0;i<scb2;i++)if(so[i]!=oo[i]){printf("  byte@%lu s=%02x o=%02x\n",i,so[i],oo[i]);break;}
    }
    BYTE* ro=(BYTE*)malloc(scb+16); DWORD rcb=0,rsk=0,rff=0; ref_any_decode(s,ro,&rcb,&rsk,&rff);
    if(rcb!=scb2||rsk!=ssk2||rff!=sff2||memcmp(ro,so,rcb)){ printf("FAIL [%s] oracle mismatch (ff r=%lu s=%lu)\n",what,rff,sff2); ++failures; }
    free(so);free(oo);free(ro);
}
int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll"); sys=(fn)GetProcAddress(h,"CryptStringToBinaryA");
    wia_b64rev_init();
    if(!sys){printf("no CryptStringToBinaryA\n");return 2;}
    for(int n=1;n<=500;n++){
        BYTE* d=(BYTE*)malloc(n); for(int i=0;i<n;i++)d[i]=(BYTE)(i*7+n*3);
        DWORD c=0;CryptBinaryToStringA(d,n,0x0,NULL,&c);char*pem=(char*)malloc(c+40);DWORD c2=c;CryptBinaryToStringA(d,n,0x0,pem,&c2);
        chk(pem,"pem");
        DWORD e=0;CryptBinaryToStringA(d,n,0x1,NULL,&e);char*b64=(char*)malloc(e+40);DWORD e2=e;CryptBinaryToStringA(d,n,0x1,b64,&e2);
        chk(b64,"plainb64");
        DWORD f=0;CryptBinaryToStringA(d,n,0x40000001,NULL,&f);char*b64n=(char*)malloc(f+40);DWORD f2=f;CryptBinaryToStringA(d,n,0x40000001,b64n,&f2);
        chk(b64n,"plainb64-nocrlf");
        free(pem);free(b64);free(b64n);free(d);
    }
    // --- regression: empty (in scope) + valid plain base64 (cch=0), direct vs live crypt32.
    // Malformed-partial inputs (stray invalid chars, bad trailing quantum) stay out of scope per RESULTS. ---
    {
        const char* T[][2]={{"","empty"},{"TWFu","plain-b64"}};
        for(int i=0;i<2;i++){
            BYTE b1[64],b2[64]; DWORD c1=sizeof(b1),s1=9,f1=9,c2=sizeof(b2),s2=9,f2=9;
            memset(b1,0x11,sizeof(b1)); memset(b2,0x22,sizeof(b2));
            int r1=wia_s2b_any(T[i][0],0,0x6,b1,&c1,&s1,&f1);
            BOOL r2=sys(T[i][0],0,0x6,b2,&c2,&s2,&f2);
            if((!!r1)!=(!!r2)||(r2&&(c1!=c2||s1!=s2||f1!=f2||memcmp(b1,b2,c1)))){
                printf("REG FAIL [%s] ours{r=%d c=%lu f=%lx} sys{r=%d c=%lu f=%lx}\n",T[i][1],r1,c1,f1,r2,c2,f2); ++failures; }
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (BASE64_ANY decode: PEM (ff=0) + plain base64 crlf/nocrlf (ff=1), n=1..500, query+convert, vs live + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
