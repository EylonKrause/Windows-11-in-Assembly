// changes/086-cryptstringtobinary-hexraw/reference.c — oracle for CryptStringToBinaryA HEXRAW decode.
typedef unsigned long DWORD; typedef unsigned char BYTE; typedef int BOOL;
extern unsigned char wia_hexrev[256];
BOOL ref_s2bh(const char* s, DWORD slen, DWORD flags, BYTE* out, DWORD* pcb, DWORD* pskip, DWORD* pflags){
    (void)flags; int hi=-1; DWORD o=0;
    for(DWORD i=0;i<slen;i++){
        unsigned char v=wia_hexrev[(unsigned char)s[i]];
        if(v==0x40) continue;
        if(v==0xFF) return 0;
        if(hi<0) hi=v; else { if(out) out[o]=(BYTE)((hi<<4)|v); o++; hi=-1; }
    }
    if(hi>=0) return 0;               // odd number of hex digits
    if(pcb) *pcb=o; if(pskip) *pskip=0; if(pflags) *pflags=0x0c;
    return 1;
}
