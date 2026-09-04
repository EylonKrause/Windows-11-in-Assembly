// changes/052-rtlintegertounicodestring/reference.c — scalar oracle for RtlIntegerToUnicodeString.
typedef long NTSTATUS;
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } U;
NTSTATUS ref_itos(unsigned long v, unsigned long base, U* s){
    if(base==0) base=10;
    if(base!=2 && base!=8 && base!=10 && base!=16) return (NTSTATUS)0xC000000D;
    unsigned short tmp[40]; int n=0;
    if(v==0) tmp[n++]='0';
    else while(v){ unsigned d=v%base; v/=base; tmp[n++]=(unsigned short)(d<10 ? '0'+d : 'A'+d-10); }
    int needed=(n+1)*2;
    if(needed > (int)s->MaximumLength) return (NTSTATUS)0x80000005;
    for(int i=0;i<n;i++) s->Buffer[i]=tmp[n-1-i];
    s->Buffer[n]=0;
    s->Length=(unsigned short)(n*2);
    return 0;
}
