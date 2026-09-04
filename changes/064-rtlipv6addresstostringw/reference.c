// changes/064-rtlipv6addresstostringw/reference.c — wide scalar oracle (mirrors validated 063).
typedef unsigned short u16;
static const char* hx="0123456789abcdef";
static u16* eh(u16* p, unsigned g){ if(g>=0x1000)*p++=hx[(g>>12)&15]; if(g>=0x100)*p++=hx[(g>>8)&15];
    if(g>=0x10)*p++=hx[(g>>4)&15]; *p++=hx[g&15]; return p; }
static u16* eo(u16* p, unsigned b){ if(b>=100){*p++='0'+b/100;b%=100;*p++='0'+b/10;*p++='0'+b%10;}
    else if(b>=10){*p++='0'+b/10;*p++='0'+b%10;} else *p++='0'+b; return p; }
u16* ref_v6fmtw(const unsigned char* a, u16* s){
    unsigned g[8]; for(int i=0;i<8;i++) g[i]=(a[2*i]<<8)|a[2*i+1];
    u16* p=s;
    int hi5=(g[0]==0&&g[1]==0&&g[2]==0&&g[3]==0&&g[4]==0);
    int embed = hi5 && ( g[5]==0x5efe || ((g[5]==0xffff||g[5]==0) && g[6]!=0) );
    if(embed){ *p++=':';*p++=':';
        if(g[5]!=0){ p=eh(p,g[5]); *p++=':'; }
        p=eo(p,a[12]);*p++='.';p=eo(p,a[13]);*p++='.';p=eo(p,a[14]);*p++='.';p=eo(p,a[15]); *p=0; return p; }
    int bs=-1,bl=0,i=0;
    while(i<8){ if(g[i]==0){ int j=i; while(j<8&&g[j]==0)j++; int len=j-i; if(len>bl){bl=len;bs=i;} i=j; } else i++; }
    if(bl<2) bs=-1;
    for(i=0;i<8;i++){ if(bs!=-1&&i>=bs&&i<bs+bl){ if(i==bs)*p++=':'; continue; }
        if(i!=0)*p++=':'; p=eh(p,g[i]); }
    if(bs!=-1&&bs+bl==8)*p++=':';
    *p=0; return p;
}
