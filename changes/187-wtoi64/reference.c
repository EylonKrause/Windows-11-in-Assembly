// changes/187-wtoi64/reference.c
// The correctness oracle: the obvious scalar _wtoi64. Not fast; just correct.
//
// Contract derived in probes/wtoi64.c and fuzz-confirmed against the live export (2,000,000 cases,
// 0 mismatches, first candidate). It is change 109's saturating 64-bit body with change 186's
// wide sets:
//   * 26 whitespace code units skipped; sign is exactly U+002D / U+002B, accepted once;
//   * the decimal-digit set is 18 contiguous blocks of ten, locale-independent;
//   * overflow SATURATES at _I64_MAX / _I64_MIN, and a magnitude of exactly 2^63 IS accepted on
//     the negative side (so -9223372036854775808 is exact, not saturated-to-the-same-value).
#include <stddef.h>

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };

static int ref_digval(unsigned c){
    for(int i=0;i<18;i++){ unsigned d=(unsigned short)(c-DBLK[i]); if(d<=9) return (int)d; }
    return -1;
}
static int ref_isws(unsigned c){
    for(int i=0;i<26;i++) if(c==WSET[i]) return 1;
    return 0;
}

__int64 ref_wtoi64(const unsigned short* s){
    const unsigned short* p = s;
    while(ref_isws(*p)) ++p;
    int neg = 0;
    if(*p=='-'){ neg=1; ++p; } else if(*p=='+'){ ++p; }
    unsigned __int64 acc = 0;
    const unsigned __int64 DIVCAP = 0x1999999999999999ULL;   /* floor((2^64-1)/10) */
    const unsigned __int64 CAP    = 0x8000000000000000ULL;   /* 2^63 */
    for(;;){
        int d = ref_digval(*p);
        if(d<0) break;
        ++p;
        if(acc >= DIVCAP){ acc = CAP; break; }               /* acc*10 would wrap */
        acc = acc*10 + (unsigned)d;
        if(acc >= CAP){ acc = CAP; break; }
    }
    if(neg) return (__int64)(0ULL - acc);                    /* 2^63 negated = _I64_MIN */
    return (acc < CAP) ? (__int64)acc : (__int64)0x7FFFFFFFFFFFFFFFLL;
}
