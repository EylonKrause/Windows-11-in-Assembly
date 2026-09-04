typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; unsigned char* Buffer; } ASTR;
extern unsigned short wia_oem2umap[256];
long ref_oem2u(USTR* dst, const ASTR* src, int alloc){
    if(alloc) return (long)0xC000000D;
    int n=src->Length;
    if(n*2 > dst->MaximumLength) return (long)0x80000005;
    dst->Length=(unsigned short)(n*2);
    for(int i=0;i<n;i++) dst->Buffer[i]=wia_oem2umap[src->Buffer[i]];
    return 0;
}
