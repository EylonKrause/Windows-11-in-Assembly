typedef unsigned short u16;
u16* ref_ip4fmtw(const unsigned char* a, u16* s){
    u16* p=s;
    for(int i=0;i<4;i++){ unsigned b=a[i];
        if(b>=100){ *p++=(u16)('0'+b/100); b%=100; *p++=(u16)('0'+b/10); *p++=(u16)('0'+b%10); }
        else if(b>=10){ *p++=(u16)('0'+b/10); *p++=(u16)('0'+b%10); }
        else *p++=(u16)('0'+b);
        if(i!=3) *p++='.';
    }
    *p=0; return p;
}
