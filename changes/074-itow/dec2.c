// changes/074-itow/dec2.c — 2-digit decimal table (00..99) as wchars.
unsigned short wia_dec2[200];
void wia_dec2_init(void){
    for(int i=0;i<100;i++){ wia_dec2[2*i]=(unsigned short)('0'+i/10); wia_dec2[2*i+1]=(unsigned short)('0'+i%10); }
}
