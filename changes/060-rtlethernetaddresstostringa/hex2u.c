// changes/060-rtlethernetaddresstostringa/hex2u.c — byte -> two UPPERCASE hex chars (packed word).
unsigned short wia_hex2u[256];
void wia_hex2u_init(void){
    static const char* h="0123456789ABCDEF";
    for(int i=0;i<256;i++) wia_hex2u[i]=(unsigned char)h[i>>4] | ((unsigned)(unsigned char)h[i&15]<<8);
}
