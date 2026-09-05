// changes/106-cryptstringtobinary-base64any/reference.c
// Scalar reference for crypt32!CryptStringToBinaryA with CRYPT_STRING_BASE64_ANY (0x6):
// if the input contains a "-----BEGIN" header -> decode as PEM (pdwFlags = 0, pdwSkip = header
// offset, body decoded from after the BEGIN line); otherwise decode the whole string as plain
// base64 (pdwFlags = 1, pdwSkip = 0). Body decode: skip non-base64, stop at '-' or '='.
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
int ref_any_decode(const char* s, BYTE* out, DWORD* pcb, DWORD* pskip, DWORD* pff){
    if(!inited) initb64();
    const char* p=strstr(s,"-----BEGIN");
    const char* q;
    if(p){ *pff=0; *pskip=(DWORD)(p-s); q=strchr(p,'\n'); if(!q){ *pcb=0; return 0; } ++q; }
    else { *pff=1; *pskip=0; q=s; }
    unsigned acc=0; int nb=0; DWORD cb=0;
    for(; *q && *q!='-'; ++q){
        unsigned char c=(unsigned char)*q;
        if(c=='=') break;
        signed char v=B64[c];
        if(v<0) continue;
        acc=(acc<<6)|(unsigned)v; nb+=6;
        if(nb>=8){ nb-=8; if(out) out[cb]=(BYTE)((acc>>nb)&0xFF); ++cb; }
    }
    *pcb=cb; return 1;
}
