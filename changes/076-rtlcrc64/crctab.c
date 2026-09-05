// changes/076-rtlcrc64/crctab.c — slicing-by-8 tables for RtlCrc64 (poly 0x9A6C9329AC4BC9B5).
// wia_crc64_tab[k][i], row-major: T[k] at byte offset k*2048, entry i at +i*8.
unsigned long long wia_crc64_tab[8][256];
void wia_crc64_init(void){
    unsigned long long P=0x9A6C9329AC4BC9B5ULL;
    for(int i=0;i<256;i++){ unsigned long long c=(unsigned long long)i;
        for(int j=0;j<8;j++) c=(c&1)?(c>>1)^P:(c>>1); wia_crc64_tab[0][i]=c; }
    for(int i=0;i<256;i++){ unsigned long long c=wia_crc64_tab[0][i];
        for(int k=1;k<8;k++){ c=(c>>8)^wia_crc64_tab[0][c&0xFF]; wia_crc64_tab[k][i]=c; } }
}
