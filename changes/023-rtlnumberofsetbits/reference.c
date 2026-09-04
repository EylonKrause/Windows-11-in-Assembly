typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
unsigned long ref_numsetbits(const RTL_BITMAP* bm){
    unsigned long n=bm->SizeOfBitMap, cnt=0, i;
    const unsigned char* b=(const unsigned char*)bm->Buffer;
    for(i=0;i<n;i++) if(b[i>>3] & (1u<<(i&7))) cnt++;
    return cnt;
}
