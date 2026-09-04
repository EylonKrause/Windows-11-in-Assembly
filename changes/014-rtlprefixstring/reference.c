typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
extern unsigned char wia_upcase_ansi[256];
unsigned char ref_prefix_a(const ASTR* s1, const ASTR* s2, int ci){
    if(s1->Length > s2->Length) return 0;
    int n=s1->Length; const unsigned char* a=(const unsigned char*)s1->Buffer,*b=(const unsigned char*)s2->Buffer;
    for(int i=0;i<n;i++){ unsigned x=a[i],y=b[i]; if(ci){x=wia_upcase_ansi[a[i]];y=wia_upcase_ansi[b[i]];} if(x!=y) return 0; }
    return 1;
}
