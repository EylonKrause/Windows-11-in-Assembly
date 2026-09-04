// oracle: x65599 hash matching ntdll!RtlHashUnicodeString (default algorithm).
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } USTR;
extern unsigned short wia_upcase[65536];
unsigned long ref_rtlhash(const USTR* s, int ci){
    unsigned long h=0; int n=s->Length/2; const unsigned short* p=s->Buffer;
    for(int i=0;i<n;i++){ unsigned long c = ci?wia_upcase[p[i]]:p[i]; h=h*65599u+c; }
    return h;
}
