// changes/083-cryptbinarytostringw-base64/reference.c — scalar oracle for CryptBinaryToStringW BASE64.
typedef unsigned long DWORD; typedef unsigned char BYTE; typedef int BOOL;
#include <wchar.h>
static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64n(const BYTE* in,int n,wchar_t* out){ int o=0,i=0;
    for(;i+3<=n;i+=3){ unsigned v=(in[i]<<16)|(in[i+1]<<8)|in[i+2];
        out[o++]=B64[(v>>18)&63];out[o++]=B64[(v>>12)&63];out[o++]=B64[(v>>6)&63];out[o++]=B64[v&63]; }
    int r=n-i; if(r==1){unsigned v=in[i]<<16;out[o++]=B64[(v>>18)&63];out[o++]=B64[(v>>12)&63];out[o++]=L'=';out[o++]=L'=';}
    else if(r==2){unsigned v=(in[i]<<16)|(in[i+1]<<8);out[o++]=B64[(v>>18)&63];out[o++]=B64[(v>>12)&63];out[o++]=B64[(v>>6)&63];out[o++]=L'=';}
    return o; }
BOOL ref_b2sw(const BYTE* pb, DWORD cb, DWORD flags, wchar_t* out, DWORD* pcch){
    if(cb==0) return 0; int n=(int)cb; int b64len=((n+2)/3)*4;
    int nocrlf=(flags & 0x40000000)!=0;
    int outlen = nocrlf ? b64len : b64len + 2*((b64len+63)/64);
    if(out==NULL){ *pcch=(DWORD)(outlen+1); return 1; }
    static wchar_t tmp[400000]; b64n(pb,n,tmp);
    if(nocrlf){ for(int i=0;i<b64len;i++) out[i]=tmp[i]; out[b64len]=0; }
    else{ int o=0,col=0; for(int i=0;i<b64len;i++){ out[o++]=tmp[i]; if(++col==64){ out[o++]=L'\r'; out[o++]=L'\n'; col=0; } }
        if(col!=0){ out[o++]=L'\r'; out[o++]=L'\n'; } out[o]=0; }
    *pcch=(DWORD)outlen; return 1;
}
