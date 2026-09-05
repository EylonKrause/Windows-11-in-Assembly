// changes/086-cryptstringtobinary-hexraw/hexrev.c
// Reverse hex table. wia_hexrev[c] = 0x00..0x0F (nibble), 0x40 (separator: space \t \n \r , -), 0xFF (invalid).
unsigned char wia_hexrev[256];
void wia_hexrev_init(void){
    for(int i=0;i<256;i++) wia_hexrev[i]=0xFF;
    for(int i=0;i<10;i++) wia_hexrev['0'+i]=(unsigned char)i;
    for(int i=0;i<6;i++){ wia_hexrev['a'+i]=(unsigned char)(10+i); wia_hexrev['A'+i]=(unsigned char)(10+i); }
    wia_hexrev[' ']=0x40; wia_hexrev['\t']=0x40; wia_hexrev['\n']=0x40; wia_hexrev['\r']=0x40;
    wia_hexrev[',']=0x40; wia_hexrev['-']=0x40;
}
