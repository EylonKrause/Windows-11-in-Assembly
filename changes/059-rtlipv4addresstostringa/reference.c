// changes/059-rtlipv4addresstostringa/reference.c — scalar oracle for RtlIpv4AddressToStringA.
char* ref_ip4fmt(const unsigned char* a, char* s){
    char* p=s;
    for(int i=0;i<4;i++){ unsigned b=a[i];
        if(b>=100){ *p++=(char)('0'+b/100); b%=100; *p++=(char)('0'+b/10); *p++=(char)('0'+b%10); }
        else if(b>=10){ *p++=(char)('0'+b/10); *p++=(char)('0'+b%10); }
        else *p++=(char)('0'+b);
        if(i!=3) *p++='.';
    }
    *p=0; return p;
}
