// changes/068-rtlipv6addresstostringexa/reference.c — scalar oracle (wraps validated 063 ref_v6fmt).
typedef long NTSTATUS;
char* ref_v6fmt(const unsigned char*, char*);   /* from 063 */
static char* du(char* p, unsigned v){ char r[12]; int n=0; do{r[n++]='0'+v%10;v/=10;}while(v); while(n)*p++=r[--n]; return p; }
NTSTATUS ref_v6ex(const unsigned char* a, unsigned long scope, unsigned short port, char* str, unsigned long* size){
    char tmp[96]; char* p=tmp;
    if(port) *p++='[';
    p = ref_v6fmt(a, p);                     /* writes IPv6 (+ a NUL); returns the NUL position */
    if(scope){ *p++='%'; p=du(p,(unsigned)scope); }
    if(port){ *p++=']'; *p++=':'; unsigned h=((port&0xff)<<8)|((port>>8)&0xff); p=du(p,h); }
    *p=0;
    unsigned needed=(unsigned)(p-tmp)+1; unsigned in=*size; *size=needed;
    if(in<needed) return (NTSTATUS)0xC000000D;
    for(unsigned i=0;i<needed;i++) str[i]=tmp[i];
    return 0;
}
