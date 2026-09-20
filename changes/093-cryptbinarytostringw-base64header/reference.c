// changes/092-cryptbinarytostring-base64header/reference.c
// Scalar reference for crypt32!CryptBinaryToStringA with the PEM-header base64 modes:
//   CRYPT_STRING_BASE64HEADER (0x0)        -> -----BEGIN CERTIFICATE-----
//   CRYPT_STRING_BASE64REQUESTHEADER (0x3) -> -----begin new certificate request-----
//   CRYPT_STRING_BASE64X509CRLHEADER (0x9) -> -----BEGIN X509 CRL-----
// Body is exactly the CRYPT_STRING_BASE64 (0x1) output: standard base64, a CRLF after
// every 64 chars including the final line. Reverse-engineered bit-exact vs the live export.
#include <windows.h>
static const char* B64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char* midof(DWORD f){ return f==0?"CERTIFICATE": f==3?"NEW CERTIFICATE REQUEST": "X509 CRL"; }
DWORD ref_b2s_hdr(const BYTE* in, DWORD n, DWORD f, char* out){
    const char* mid=midof(f); DWORD len=0, col=0, i=0;
    #define P(c)   do{ if(out) out[len]=(char)(c); ++len; }while(0)
    #define S(str) do{ const char* s=(str); while(*s){ P(*s); ++s; } }while(0)
    #define EMIT(c) do{ P(c); if(++col==64){ P('\r'); P('\n'); col=0; } }while(0)
    S("-----BEGIN "); S(mid); S("-----\r\n");
    while(i+3<=n){ DWORD v=(in[i]<<16)|(in[i+1]<<8)|in[i+2];
        EMIT(B64[(v>>18)&63]);EMIT(B64[(v>>12)&63]);EMIT(B64[(v>>6)&63]);EMIT(B64[v&63]); i+=3; }
    if(n-i==2){ DWORD v=(in[i]<<16)|(in[i+1]<<8);
        EMIT(B64[(v>>18)&63]);EMIT(B64[(v>>12)&63]);EMIT(B64[(v>>6)&63]);EMIT('='); }
    else if(n-i==1){ DWORD v=(in[i]<<16);
        EMIT(B64[(v>>18)&63]);EMIT(B64[(v>>12)&63]);EMIT('=');EMIT('='); }
    if(col!=0){ P('\r'); P('\n'); }
    S("-----END "); S(mid); S("-----\r\n");
    if(out) out[len]=0;
    return len;
    #undef P
    #undef S
    #undef EMIT
}
