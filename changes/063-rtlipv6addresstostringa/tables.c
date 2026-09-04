// changes/063-rtlipv6addresstostringa/tables.c — lowercase hex + decimal byte tables.
unsigned char wia_hex2b[512];   // byte -> 2 lowercase hex bytes
unsigned char wia_dec2b[200];   // 0..99 -> 2 decimal bytes
unsigned char wia_hex1[16];     // nibble -> lowercase hex char
void wia_v6tables_init(void){
    static const char* h="0123456789abcdef";
    for(int i=0;i<256;i++){ wia_hex2b[2*i]=(unsigned char)h[i>>4]; wia_hex2b[2*i+1]=(unsigned char)h[i&15]; }
    for(int i=0;i<100;i++){ wia_dec2b[2*i]=(unsigned char)('0'+i/10); wia_dec2b[2*i+1]=(unsigned char)('0'+i%10); }
    for(int i=0;i<16;i++) wia_hex1[i]=(unsigned char)h[i];
}
