// changes/082-cryptstringtobinary-base64/reference.c
// Scalar oracle for CryptStringToBinaryA, CRYPT_STRING_BASE64 (RE'd + validated 0-mismatch vs the
// live export over 120,000 inputs). Scope: valid base64 (whitespace-skipped, proper padding) -> bytes;
// *pdwSkip=0, *pdwFlags=1; query (pbBinary==NULL) -> *pcbBinary = byte count; invalid char / data after
// padding -> FALSE. The quirky malformed-input edges (lone trailing char, leading/extra padding) are NOT
// modelled (documented out of scope in RESULTS.md).
typedef unsigned long DWORD; typedef unsigned char BYTE; typedef int BOOL;
extern unsigned char wia_b64rev[256];
BOOL ref_s2b(const char* s, DWORD slen, DWORD flags, BYTE* out, DWORD* pcb, DWORD* pskip, DWORD* pflags){
    (void)flags;
    unsigned acc=0; int bits=0, pad=0; DWORD o=0;
    for(DWORD i=0;i<slen;i++){
        unsigned char v=wia_b64rev[(unsigned char)s[i]];
        if(v==0x41) continue;              // whitespace
        if(v==0x40){ pad=1; continue; }    // padding
        if(v==0xFF) return 0;              // invalid
        if(pad) return 0;                  // data after padding
        acc=(acc<<6)|v; bits+=6;
        if(bits>=8){ bits-=8; if(out) out[o]=(BYTE)(acc>>bits); o++; }
    }
    if(pcb) *pcb=o;
    if(pskip) *pskip=0;
    if(pflags) *pflags=1;
    return 1;
}
