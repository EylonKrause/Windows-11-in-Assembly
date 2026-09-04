typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
unsigned char ref_arebitsset(const RTL_BITMAP* bm, unsigned long start, unsigned long len){
    if(len==0) return 0;
    unsigned long long end=(unsigned long long)start+len;
    if(end > bm->SizeOfBitMap) return 0;
    const unsigned char* b=(const unsigned char*)bm->Buffer;
    for(unsigned long i=start;i<end;i++) if(!((b[i>>3]>>(i&7))&1)) return 0;
    return 1;
}
