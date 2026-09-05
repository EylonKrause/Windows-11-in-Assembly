// changes/090-cryptbinarytostring-hexfmt/reference.c
// Scalar reference for crypt32!CryptBinaryToStringA with the *formatted* hex modes:
//   CRYPT_STRING_HEX (0x4), CRYPT_STRING_HEXASCII (0x5),
//   CRYPT_STRING_HEXADDR (0xa), CRYPT_STRING_HEXASCIIADDR (0xb).
// Reverse-engineered to be bit-exact vs the live export (validated n=1..600 + >64KB).
//
// Layout (16 bytes/line):
//   [ADDR: lowercase hex offset, min 4 digits, '\t']
//   hex bytes: "XX", single space between, a DOUBLE space after byte 8, no trailing space
//   [ASCII: pad the hex field with spaces to column 51, then one char/byte:
//           printable 0x20..0x7e -> itself, else '.']
//   "\r\n"
// Returns the length excluding the NUL; writes NUL at out[len] when out != NULL.
#include <windows.h>
static const char* HEXD = "0123456789abcdef";
DWORD ref_b2shf(const BYTE* in, DWORD n, DWORD flags, char* out){
    int ADDR = (flags & 0x2) != 0;   // 0xa,0xb
    int ASC  = (flags & 0x1) != 0;   // 0x5,0xb
    DWORD len = 0, off = 0;
    #define PUT(ch) do{ if(out) out[len]=(char)(ch); ++len; }while(0)
    if(n==0){ if(out) out[0]=0; return 0; }
    while(off < n){
        DWORD k = n - off; if(k > 16) k = 16;
        if(ADDR){
            DWORD a = off; char tmp[16]; int d = 0;
            if(a==0) tmp[d++]='0'; else while(a){ tmp[d++]=HEXD[a & 0xf]; a >>= 4; }
            while(d < 4) tmp[d++]='0';
            for(int i=d-1;i>=0;--i) PUT(tmp[i]);
            PUT('\t');
        }
        DWORD hexw = 0;
        for(DWORD j=0;j<k;++j){
            if(j){ PUT(' '); ++hexw; if(j==8){ PUT(' '); ++hexw; } }
            BYTE b = in[off+j];
            PUT(HEXD[b>>4]); PUT(HEXD[b&0xf]); hexw += 2;
        }
        if(ASC){
            while(hexw < 51){ PUT(' '); ++hexw; }
            for(DWORD j=0;j<k;++j){ BYTE b=in[off+j]; PUT((b>=0x20 && b<=0x7e)? b : '.'); }
        }
        PUT('\r'); PUT('\n');
        off += k;
    }
    if(out) out[len]=0;
    return len;
    #undef PUT
}
