typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; unsigned char* Buffer; } ASTR;
extern unsigned char wia_upansimap[65536];
long ref_u2au(ASTR* dst, const USTR* src, int alloc){
    if(alloc) return (long)0xC000000D;
    int n=src->Length/2;
    if(n > dst->MaximumLength) return (long)0x80000005;
    dst->Length=(unsigned short)n;
    for(int i=0;i<n;i++) dst->Buffer[i]=wia_upansimap[src->Buffer[i]];
    return 0;
}
