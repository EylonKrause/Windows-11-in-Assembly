// CORRECTED 2026-09-20: this required only room for the conversion and wrote no terminator, which
// matched the implementation exactly and the export on neither count. The export NUL-TERMINATES, so
// it needs room for the conversion PLUS one element, and on overflow it writes NOTHING and leaves
// dst->Length as the caller had it. (Its sibling 018 is the odd one out: that one truncates and
// partially writes. Four functions in one family, two failure disciplines.) Found by live
// substitution; the probe output is recorded in RESULTS.md.
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; unsigned char* Buffer; } ASTR;
extern unsigned short wia_oem2umap[256];
long ref_oem2u(USTR* dst, const ASTR* src, int alloc){
    if(alloc) return (long)0xC000000D;
    int n=src->Length;
    if(n*2 + 2 > dst->MaximumLength) return (long)0x80000005;
    dst->Length=(unsigned short)(n*2);
    for(int i=0;i<n;i++) dst->Buffer[i]=wia_oem2umap[src->Buffer[i]];
    dst->Buffer[n]=0;
    return 0;
}
