// changes/082-cryptstringtobinary-base64/revtab.c
// Reverse base64 table for the decoder. wia_b64rev[c] =
//   0x00..0x3F : the 6-bit value of a base64 char
//   0x40       : '=' padding
//   0x41       : whitespace (skip): space, \t, \n, \v, \f, \r
//   0xFF       : invalid
unsigned char wia_b64rev[256];
void wia_b64rev_init(void){
    static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(int i=0;i<256;i++) wia_b64rev[i]=0xFF;
    for(int i=0;i<64;i++) wia_b64rev[(unsigned char)B64[i]]=(unsigned char)i;
    wia_b64rev['=']=0x40;
    wia_b64rev[' ']=0x41; wia_b64rev['\t']=0x41; wia_b64rev['\n']=0x41;
    wia_b64rev['\v']=0x41; wia_b64rev['\f']=0x41; wia_b64rev['\r']=0x41;
}
