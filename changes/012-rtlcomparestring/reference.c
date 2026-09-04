typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
extern unsigned char wia_upcase_ansi[256];
long ref_cmpstr(const ASTR* s1, const ASTR* s2, int ci){
    int n1=s1->Length,n2=s2->Length,n=n1<n2?n1:n2;
    const unsigned char* a=(const unsigned char*)s1->Buffer,*b=(const unsigned char*)s2->Buffer;
    for(int i=0;i<n;i++){ unsigned x=a[i],y=b[i]; if(ci){x=wia_upcase_ansi[a[i]];y=wia_upcase_ansi[b[i]];} if(x!=y) return (long)x-(long)y; }
    return (long)n1-(long)n2;
}
