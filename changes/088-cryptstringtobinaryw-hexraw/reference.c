// changes/088-cryptstringtobinaryw-hexraw/reference.c — oracle for CryptStringToBinaryW HEXRAW decode.
#include <wchar.h>
typedef unsigned long DWORD; typedef unsigned char BYTE; typedef int BOOL;
extern unsigned char wia_hexrev[256];
BOOL ref_s2bhw(const wchar_t* s, DWORD slen, DWORD flags, BYTE* out, DWORD* pcb, DWORD* pskip, DWORD* pflags){
    (void)flags; int hi=-1; DWORD o=0;
    for(DWORD i=0;i<slen;i++){
        unsigned w=(unsigned)s[i];
        if(w>=0x100) return 0;
        unsigned char v=wia_hexrev[w];
        if(v==0x40) continue;
        if(v==0xFF) return 0;
        if(hi<0) hi=v; else { if(out) out[o]=(BYTE)((hi<<4)|v); o++; hi=-1; }
    }
    if(hi>=0) return 0;
    if(pcb) *pcb=o; if(pskip) *pskip=0; if(pflags) *pflags=0x0c;
    return 1;
}
