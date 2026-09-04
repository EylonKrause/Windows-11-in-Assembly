typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } USTR;
extern unsigned short wia_upcase[65536];
unsigned char ref_prefix(const USTR* s1, const USTR* s2, int ci){
    if(s1->Length > s2->Length) return 0;
    int n=s1->Length/2; const unsigned short* a=s1->Buffer; const unsigned short* b=s2->Buffer;
    for(int i=0;i<n;i++){ unsigned x=a[i], y=b[i]; if(ci){x=wia_upcase[a[i]];y=wia_upcase[b[i]];} if(x!=y) return 0; }
    return 1;
}
