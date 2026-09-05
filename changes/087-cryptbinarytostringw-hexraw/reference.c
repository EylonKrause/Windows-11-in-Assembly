// changes/087-cryptbinarytostringw-hexraw/reference.c — oracle for CryptBinaryToStringW HEXRAW.
#include <stddef.h>
#include <wchar.h>
typedef unsigned long DWORD; typedef unsigned char BYTE; typedef int BOOL;
static const char HX[]="0123456789abcdef";
BOOL ref_b2shw(const BYTE* pb, DWORD cb, DWORD flags, wchar_t* out, DWORD* pcch){
    if(cb==0) return 0;
    int n=(int)cb, outlen=2*n + ((flags & 0x40000000)?0:2);
    if(out==NULL){ *pcch=(DWORD)(outlen+1); return 1; }
    int o=0; for(int i=0;i<n;i++){ out[o++]=HX[pb[i]>>4]; out[o++]=HX[pb[i]&15]; }
    if(!(flags & 0x40000000)){ out[o++]=L'\r'; out[o++]=L'\n'; }
    out[o]=0; *pcch=(DWORD)outlen; return 1;
}
