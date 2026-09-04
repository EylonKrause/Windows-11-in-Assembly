typedef long NTSTATUS; typedef unsigned short u16;
u16* ref_v6fmtw(const unsigned char*, u16*);   /* from 064 */
static u16* du(u16* p, unsigned v){ u16 r[12]; int n=0; do{r[n++]='0'+v%10;v/=10;}while(v); while(n)*p++=r[--n]; return p; }
NTSTATUS ref_v6exw(const unsigned char* a, unsigned long scope, unsigned short port, u16* str, unsigned long* size){
    u16 tmp[100]; u16* p=tmp;
    if(port) *p++='[';
    p = ref_v6fmtw(a, p);
    if(scope){ *p++='%'; p=du(p,(unsigned)scope); }
    if(port){ *p++=']'; *p++=':'; unsigned h=((port&0xff)<<8)|((port>>8)&0xff); p=du(p,h); }
    *p=0;
    unsigned needed=(unsigned)(p-tmp)+1; unsigned in=*size; *size=needed;
    if(in<needed) return (NTSTATUS)0xC000000D;
    for(unsigned i=0;i<needed;i++) str[i]=tmp[i];
    return 0;
}
