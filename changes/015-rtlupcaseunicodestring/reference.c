typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } USTR;
extern unsigned short wia_upcase[65536];
long ref_upcasestr(USTR* dst, const USTR* src, int alloc){
    if(alloc) return (long)0xC000000D;
    if(src->Length > dst->MaximumLength) return (long)0x80000005;
    int n=src->Length/2; dst->Length=src->Length;
    for(int i=0;i<n;i++) dst->Buffer[i]=wia_upcase[src->Buffer[i]];
    return 0;
}
