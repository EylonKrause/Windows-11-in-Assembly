// changes/104-cryptstringtobinary-base64header/reference.c
// Scalar reference for crypt32!CryptStringToBinaryA with CRYPT_STRING_BASE64HEADER (0x0):
// PEM decode. Find "-----BEGIN" (pdwSkip = its offset), skip to the end of that line, then
// base64-decode the body (skipping whitespace/newlines) until '-' (start of "-----END") or a
// '=' pad. Reverse-engineered bit-exact vs the live export. *pdwFlags = 0.
#include <windows.h>
#include <string.h>
static signed char B64[256];
static int inited=0;
static void initb64(void){
    for(int i=0;i<256;i++) B64[i]=-1;
    const char* a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(int i=0;i<64;i++) B64[(unsigned char)a[i]]=(signed char)i;
    inited=1;
}
// returns 1 on success (found a header), 0 otherwise. Writes bytes to out (if non-NULL),
// count to *pcb, header offset to *pskip.
int ref_pem_decode(const char* s, BYTE* out, DWORD* pcb, DWORD* pskip){
    if(!inited) initb64();
    const char* p=strstr(s,"-----BEGIN");
    if(!p){ *pcb=0; *pskip=0; return 0; }
    *pskip=(DWORD)(p-s);
    const char* q=strchr(p,'\n'); if(!q){ *pcb=0; return 0; } ++q;   // past the BEGIN line
    unsigned acc=0; int nb=0; DWORD cb=0;
    for(; *q && *q!='-'; ++q){
        unsigned char c=(unsigned char)*q;
        if(c=='=') break;
        signed char v=B64[c];
        if(v<0) continue;                       // whitespace / newline / stray char -> skip
        acc=(acc<<6)|(unsigned)v; nb+=6;
        if(nb>=8){ nb-=8; if(out) out[cb]=(BYTE)((acc>>nb)&0xFF); ++cb; }
    }
    *pcb=cb; return 1;
}
