// changes/058-rtlstringfromguidex/hex2.c — byte -> two lowercase hex wchars, packed in a dword.
unsigned int wia_hex2[256];
void wia_hex2_init(void){
    static const char* h="0123456789abcdef";
    for(int i=0;i<256;i++) wia_hex2[i]=(unsigned)(unsigned char)h[i>>4] | ((unsigned)(unsigned char)h[i&15]<<16);
}
