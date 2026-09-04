// changes/054-ultoa/dec2b.c — 2-digit decimal byte table (00..99).
unsigned char wia_dec2b[200];
void wia_dec2b_init(void){
    for(int i=0;i<100;i++){ wia_dec2b[2*i]=(unsigned char)('0'+i/10); wia_dec2b[2*i+1]=(unsigned char)('0'+i%10); }
}
