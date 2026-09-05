// changes/125-rtlfindclearbits/reference.c
// Oracle for ntdll!RtlFindClearBits, validated bit-exact vs the live export over 3M fuzz. Finds the
// first run of `num` clear (0) bits, scanning cyclically from the (clamped) hint; returns the start
// index or 0xFFFFFFFF. num==0 -> byte-floored clamped hint; num>n -> -1.
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
static int getbit(const unsigned long* b, unsigned long i){ return (b[i>>5]>>(i&31))&1u; }
unsigned long ref_findclearbits(const RTL_BITMAP* bm, unsigned long num, unsigned long hint){
    unsigned long n=bm->SizeOfBitMap; const unsigned long* b=bm->Buffer;
    unsigned long h=(hint>=n)?0:hint;
    if(num==0) return h & ~7u;
    if(num>n) return 0xFFFFFFFFul;
    unsigned long range=n-num+1;                    // valid start positions 0..n-num
    unsigned long start=(h<=n-num)?h:0;
    for(unsigned long k=0;k<range;k++){
        unsigned long p=start+k; if(p>=range) p-=range;
        unsigned long j=0; for(;j<num;j++){ if(getbit(b,p+j)!=0) break; }
        if(j==num) return p;
    }
    return 0xFFFFFFFFul;
}
