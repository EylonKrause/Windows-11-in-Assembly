// changes/067-rtlconvertsidtounicodestring/reference.c — scalar oracle (validated 0/2,000,000 vs ntdll).
typedef long NTSTATUS;
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } U;
static const char* HX="0123456789ABCDEF";
static unsigned short* du(unsigned short* p, unsigned v){ unsigned short r[12]; int n=0; do{ r[n++]='0'+v%10; v/=10; }while(v); while(n)*p++=r[--n]; return p; }
NTSTATUS ref_sidfmt(U* out, const unsigned char* sid){
    unsigned rev=sid[0], cnt=sid[1];
    if(rev!=1) return (NTSTATUS)0xC0000078;
    unsigned short tmp[300]; unsigned short* p=tmp;
    *p++='S'; *p++='-'; p=du(p,rev); *p++='-';
    unsigned long long auth=0; for(int i=0;i<6;i++) auth=(auth<<8)|sid[2+i];
    if(auth<0x100000000ULL) p=du(p,(unsigned)auth);
    else { *p++='0'; *p++='x'; unsigned short r[12]; int n=0; unsigned long long a=auth; do{ r[n++]=HX[a&0xF]; a>>=4; }while(a); while(n)*p++=r[--n]; }
    for(unsigned i=0;i<cnt;i++){ *p++='-'; unsigned sa; const unsigned char* q=sid+8+4*i; sa=q[0]|(q[1]<<8)|(q[2]<<16)|((unsigned)q[3]<<24); p=du(p,sa); }
    unsigned len=(unsigned)((p-tmp)*2);
    if(out->MaximumLength < len+2) return (NTSTATUS)0x80000005;
    for(unsigned i=0;i<len/2;i++) out->Buffer[i]=tmp[i];
    out->Buffer[len/2]=0; out->Length=(unsigned short)len;
    return 0;
}
