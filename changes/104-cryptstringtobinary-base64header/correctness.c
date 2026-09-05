// changes/104-cryptstringtobinary-base64header/correctness.c
// Bit-exact vs live crypt32!CryptStringToBinaryA (BASE64HEADER) AND the scalar oracle:
// return, cbBinary, pdwSkip, pdwFlags, decoded bytes; query + convert; canonical PEM plus
// leading/trailing garbage and multi-line bodies.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int wia_s2b_pem(const char*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern void wia_b64rev_init(void);
int ref_pem_decode(const char*, BYTE*, DWORD*, DWORD*);
typedef BOOL (WINAPI *fn)(LPCSTR,DWORD,DWORD,BYTE*,DWORD*,DWORD*,DWORD*);
static fn sys;
static int failures=0;
static void chk(const char* pem, const char* what){
    // query
    DWORD scb=0,ssk=0,sff=0; BOOL sr=sys(pem,0,0x0,NULL,&scb,&ssk,&sff);
    DWORD ocb=0,osk=0,off=0;  int  orr=wia_s2b_pem(pem,0,0x0,NULL,&ocb,&osk,&off);
    if((!!sr)!=(!!orr)){ printf("FAIL [%s] query ret sys=%d ours=%d\n",what,sr,orr); ++failures; return; }
    if(!sr) return;    // both failed -> ok (not a valid PEM)
    // convert
    BYTE* so=(BYTE*)malloc(scb+16); BYTE* oo=(BYTE*)malloc(scb+16);
    DWORD scb2=scb,ssk2=0,sff2=0; BOOL sr2=sys(pem,0,0x0,so,&scb2,&ssk2,&sff2);
    DWORD ocb2=scb,osk2=0,off2=0; int  or2=wia_s2b_pem(pem,0,0x0,oo,&ocb2,&osk2,&off2);
    if((!!sr2)!=(!!or2) || scb2!=ocb2 || ssk2!=osk2 || sff2!=off2 || memcmp(so,oo,scb2)!=0){
        printf("FAIL [%s] sys{ok=%d cb=%lu sk=%lu ff=%lx} ours{ok=%d cb=%lu sk=%lu ff=%lx}\n",
            what,sr2,scb2,ssk2,sff2,or2,ocb2,osk2,off2); ++failures;
        for(DWORD i=0;i<scb2;i++) if(so[i]!=oo[i]){ printf("  byte@%lu sys=%02x ours=%02x\n",i,so[i],oo[i]); break; }
    }
    // also cross-check the C oracle
    BYTE* ro=(BYTE*)malloc(scb+16); DWORD rcb=0,rsk=0; ref_pem_decode(pem,ro,&rcb,&rsk);
    if(rcb!=scb2 || rsk!=ssk2 || memcmp(ro,so,rcb)!=0){ printf("FAIL [%s] oracle mismatch\n",what); ++failures; }
    free(so); free(oo); free(ro);
}
int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll");
    sys=(fn)GetProcAddress(h,"CryptStringToBinaryA");
    wia_b64rev_init();
    if(!sys){ printf("no CryptStringToBinaryA\n"); return 2; }
    for(int n=1;n<=500;n++){
        BYTE* d=(BYTE*)malloc(n); for(int i=0;i<n;i++) d[i]=(BYTE)(i*7+n*3);
        DWORD c=0; CryptBinaryToStringA(d,n,0x0,NULL,&c); char* pem=(char*)malloc(c+80); DWORD c2=c;
        CryptBinaryToStringA(d,n,0x0,pem,&c2);
        chk(pem,"canonical");
        char* g=(char*)malloc(c+120); sprintf(g,"garbage line1\r\nx\r\n%s",pem); chk(g,"leadgarbage"); free(g);
        char* t=(char*)malloc(c+120); sprintf(t,"%strailing after end\r\nmore\r\n",pem); chk(t,"trailgarbage"); free(t);
        free(pem); free(d);
    }
    // no header at all -> both fail
    chk("not a pem at all\r\njust text\r\n","noheader");
    if(!failures) printf("CORRECTNESS: PASS (BASE64HEADER decode: canonical + lead/trail garbage + multi-line + no-header, n=1..500, query+convert, vs live crypt32 + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
