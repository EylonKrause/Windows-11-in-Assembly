// oracle model of RtlCompareUnicodeString (sign-significant), using wia_upcase.
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } USTR;
extern unsigned short wia_upcase[65536];
long ref_cmp_ustr(const USTR* s1, const USTR* s2, int ci){
    int n1=s1->Length/2, n2=s2->Length/2, n=n1<n2?n1:n2;
    const unsigned short* a=s1->Buffer; const unsigned short* b=s2->Buffer;
    for(int i=0;i<n;i++){ unsigned x=a[i], y=b[i]; if(ci){x=wia_upcase[a[i]];y=wia_upcase[b[i]];} if(x!=y) return (long)x-(long)y; }
    return (long)s1->Length - (long)s2->Length;
}
