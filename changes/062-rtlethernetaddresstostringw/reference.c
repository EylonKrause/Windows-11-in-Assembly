typedef unsigned short u16;
u16* ref_macfmtw(const unsigned char* a, u16* s){
    static const char* h="0123456789ABCDEF"; u16* p=s;
    for(int i=0;i<6;i++){ *p++=(u16)h[a[i]>>4]; *p++=(u16)h[a[i]&15]; if(i!=5)*p++='-'; }
    *p=0; return p;
}
