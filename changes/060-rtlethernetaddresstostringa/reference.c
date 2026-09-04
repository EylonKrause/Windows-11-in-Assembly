// changes/060-rtlethernetaddresstostringa/reference.c — scalar oracle for RtlEthernetAddressToStringA.
char* ref_macfmt(const unsigned char* a, char* s){
    static const char* h="0123456789ABCDEF"; char* p=s;
    for(int i=0;i<6;i++){ *p++=h[a[i]>>4]; *p++=h[a[i]&15]; if(i!=5)*p++='-'; }
    *p=0; return p;
}
