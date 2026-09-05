// changes/081-cryptbinarytostring-base64/reference.c
// Scalar oracle for CryptBinaryToStringA, CRYPT_STRING_BASE64 format (RE'd + validated 0-mismatch
// vs the live crypt32 export over 60,000 inputs, both NOCRLF and default-CRLF, incl. the query-size
// mode). Scope: BASE64 with/without CRLF; query (pszString==NULL); n==0 -> FALSE. The quirky
// too-small-buffer partial-write path is NOT modelled (documented out of scope in RESULTS.md).
#include <stddef.h>
typedef unsigned long DWORD; typedef unsigned char BYTE; typedef int BOOL;
#define CRYPT_STRING_BASE64 0x00000001
#define CRYPT_STRING_NOCRLF 0x40000000
static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_nocrlf(const BYTE* in, int n, char* out){
    int o=0,i=0;
    for(; i+3<=n; i+=3){ unsigned v=(in[i]<<16)|(in[i+1]<<8)|in[i+2];
        out[o++]=B64[(v>>18)&63]; out[o++]=B64[(v>>12)&63]; out[o++]=B64[(v>>6)&63]; out[o++]=B64[v&63]; }
    int r=n-i;
    if(r==1){ unsigned v=in[i]<<16; out[o++]=B64[(v>>18)&63]; out[o++]=B64[(v>>12)&63]; out[o++]='='; out[o++]='='; }
    else if(r==2){ unsigned v=(in[i]<<16)|(in[i+1]<<8); out[o++]=B64[(v>>18)&63]; out[o++]=B64[(v>>12)&63]; out[o++]=B64[(v>>6)&63]; out[o++]='='; }
    return o;
}
// Returns 0 on failure (n==0), else 1; sets *pcch. If out==NULL -> query (*pcch = needed incl NUL).
BOOL ref_b2s(const BYTE* pb, DWORD cb, DWORD flags, char* out, DWORD* pcch){
    if(cb==0) return 0;
    int n=(int)cb;
    int b64len=((n+2)/3)*4;
    int nocrlf=(flags & CRYPT_STRING_NOCRLF)!=0;
    int outlen = nocrlf ? b64len : b64len + 2*((b64len+63)/64);
    if(out==NULL){ *pcch=(DWORD)(outlen+1); return 1; }
    // (sufficient-buffer path)
    static char tmp[400000];
    b64_nocrlf(pb,n,tmp);
    if(nocrlf){ for(int i=0;i<b64len;i++) out[i]=tmp[i]; out[b64len]=0; }
    else{
        int o=0,col=0;
        for(int i=0;i<b64len;i++){ out[o++]=tmp[i]; if(++col==64){ out[o++]='\r'; out[o++]='\n'; col=0; } }
        if(col!=0){ out[o++]='\r'; out[o++]='\n'; }
        out[o]=0;
    }
    *pcch=(DWORD)outlen;
    return 1;
}
