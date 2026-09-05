// changes/130-rtlsetbits/reference.c
// Oracle for ntdll!RtlSetBits: set bits [start, start+num). No bounds check against SizeOfBitMap --
// the live export writes past it if asked (verified), so the oracle must too.
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
void ref_setbits(RTL_BITMAP* bm, unsigned long start, unsigned long num){
    unsigned long* b=bm->Buffer;
    for(unsigned long i=0;i<num;i++){ unsigned long j=start+i; b[j>>5] |= 1ul<<(j&31); }
}
