typedef long NTSTATUS; typedef unsigned short u16;
static u16* eo(u16* p, unsigned b){ if(b>=100){*p++='0'+b/100;b%=100;*p++='0'+b/10;*p++='0'+b%10;}
    else if(b>=10){*p++='0'+b/10;*p++='0'+b%10;} else *p++='0'+b; return p; }
NTSTATUS ref_ip4exw(const unsigned char* a, unsigned short port, u16* str, unsigned long* size){
    u16 tmp[32]; u16* p=tmp;
    for(int i=0;i<4;i++){ p=eo(p,a[i]); if(i!=3)*p++='.'; }
    if(port){ *p++=':'; unsigned h=((port&0xff)<<8)|((port>>8)&0xff);
        char rb[8]; int n=0; do{ rb[n++]='0'+h%10; h/=10; }while(h); while(n)*p++=rb[--n]; }
    *p=0;
    unsigned needed=(unsigned)(p-tmp)+1; unsigned in=*size; *size=needed;
    if(in<needed) return (NTSTATUS)0xC000000D;
    for(unsigned i=0;i<needed;i++) str[i]=tmp[i];
    return 0;
}
