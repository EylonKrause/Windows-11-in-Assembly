// changes/018-rtlunicodestringtoansistring/reference.c
// Oracle for ntdll!RtlUnicodeStringToAnsiString (Allocate = FALSE).
//
// CORRECTED 2026-09-20: this used to require only `n <= MaximumLength` and to write no terminator,
// which matched the implementation exactly and matched the export on neither count. The export
// NUL-TERMINATES and TRUNCATES:
//
//     MaximumLength == 0  ->  STATUS_BUFFER_OVERFLOW, nothing written, Length left alone
//     otherwise           ->  n = min(srclen, MaximumLength - 1) characters converted,
//                             a NUL written at [n], Length = n, and the status is
//                             STATUS_SUCCESS when n == srclen, STATUS_BUFFER_OVERFLOW otherwise
//
// so a source of 8 characters with MaximumLength 4 comes back as "ABC\0" with Length 3, a partial
// write on failure. Its siblings 019, 020, 024 and 025 all refuse outright instead and leave the
// destination untouched; this one alone truncates. Measured in live-substitution, probe output
// recorded in RESULTS.md.
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; unsigned char* Buffer; } ASTR;
extern unsigned char wia_ansimap[65536];
long ref_u2a(ASTR* dst, const USTR* src, int alloc){
    if(alloc) return (long)0xC000000D;
    int n=src->Length/2;
    int avail, status=0;
    if(dst->MaximumLength == 0) return (long)0x80000005;
    avail = dst->MaximumLength - 1;
    if(n > avail){ n = avail; status = (long)0x80000005; }
    dst->Length=(unsigned short)n;
    for(int i=0;i<n;i++) dst->Buffer[i]=wia_ansimap[src->Buffer[i]];
    dst->Buffer[n]=0;
    return status;
}
