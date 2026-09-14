// changes/192-rtlarebitsclear/reference.c
// The correctness oracle: the obvious per-bit RtlAreBitsClear. Not fast; just correct.
// Every edge here was re-derived in probes/abc.c against the live export rather than mirrored
// from change 030 -- len 0 is FALSE (not "vacuously clear"), an out-of-range or ULONG-wrapping
// range is FALSE, and start == SizeOfBitMap is FALSE for any len. 400 000 randomized bitmaps
// agreed with the live export on every case.
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
unsigned char ref_arebitsclear(const RTL_BITMAP* bm, unsigned long start, unsigned long len){
    if(len==0) return 0;
    unsigned long long end=(unsigned long long)start+len;
    if(end > bm->SizeOfBitMap) return 0;
    const unsigned char* b=(const unsigned char*)bm->Buffer;
    for(unsigned long i=start;i<end;i++) if((b[i>>3]>>(i&7))&1) return 0;
    return 1;
}
