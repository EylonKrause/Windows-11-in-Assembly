// changes/084-cryptstringtobinaryw-base64/reference.c — scalar oracle for CryptStringToBinaryW BASE64.
#include <wchar.h>
typedef unsigned long DWORD; typedef unsigned char BYTE; typedef int BOOL;
extern unsigned char wia_b64rev[256];
BOOL ref_s2bw(const wchar_t* s, DWORD slen, DWORD flags, BYTE* out, DWORD* pcb, DWORD* pskip, DWORD* pflags){
    (void)flags; unsigned acc=0; int bits=0, pad=0; DWORD o=0;
    for(DWORD i=0;i<slen;i++){
        unsigned w=(unsigned)s[i];
        if(w>=0x100) return 0;
        unsigned char v=wia_b64rev[w];
        if(v==0x41) continue;
        if(v==0x40){ pad=1; continue; }
        if(v==0xFF) return 0;
        if(pad) return 0;
        acc=(acc<<6)|v; bits+=6;
        if(bits>=8){ bits-=8; if(out) out[o]=(BYTE)(acc>>bits); o++; }
    }
    if(pcb) *pcb=o; if(pskip) *pskip=0; if(pflags) *pflags=1;
    return 1;
}
