// changes/124-rtlnumberofclearbits/reference.c
// Oracle for ntdll!RtlNumberOfClearBits: count of clear (0) bits in the first SizeOfBitMap bits.
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
unsigned long ref_numclearbits(const RTL_BITMAP* bm){
    unsigned long n=bm->SizeOfBitMap; const unsigned long* b=bm->Buffer; unsigned long c=0;
    for(unsigned long i=0;i<n;i++) if(((b[i>>5]>>(i&31))&1u)==0) c++;
    return c;
}
