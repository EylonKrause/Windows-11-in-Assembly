// changes/064-rtlipv6addresstostringw/tables.c — wide lowercase hex + decimal tables.
unsigned int   wia_hex2wt[256];  // byte -> 2 lowercase hex wchars (dword)
unsigned int   wia_dec2wt[100];  // 0..99 -> 2 decimal wchars (dword)
unsigned short wia_hex1w[16];    // nibble -> lowercase hex wchar
void wia_v6wtables_init(void){
    static const char* h="0123456789abcdef";
    for(int i=0;i<256;i++) wia_hex2wt[i]=(unsigned)(unsigned char)h[i>>4] | ((unsigned)(unsigned char)h[i&15]<<16);
    for(int i=0;i<100;i++) wia_dec2wt[i]=(unsigned)('0'+i/10) | ((unsigned)('0'+i%10)<<16);
    for(int i=0;i<16;i++) wia_hex1w[i]=(unsigned short)h[i];
}
