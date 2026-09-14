// changes/186-wtoi/reference.c
// The correctness oracle: the obvious scalar _wtoi. Not fast; just correct.
//
// Contract derived in probes/wtoi.c and fuzz-confirmed against the live export (2,000,000 cases,
// 0 mismatches, first candidate). It is change 108's `atoi` shape with two wide-specific sets,
// both established by exhaustive 65536-code-unit sweeps and shown to be LOCALE-INDEPENDENT:
//   * 26 whitespace code units are skipped (the byte form skips six);
//   * the decimal-digit set is exactly 18 contiguous blocks of ten, each ascending 0..9, and
//     digits from DIFFERENT blocks concatenate freely ("1", U+FF12, U+0663 parses as 123);
//   * sign is exactly U+002D / U+002B, accepted once, only immediately after the whitespace run;
//   * overflow SATURATES: positive -> INT_MAX, negative -> INT_MIN.
#include <stddef.h>

/* 18 contiguous blocks of ten -- the Unicode 3.0-era Nd set, frozen in ucrtbase.
   Change 166 found the identical list frozen in ntdll; measured separately here. */
static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };

/* 26 code units skipped as leading whitespace. U+200B (ZWSP) is NOT one of them. */
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };

static int ref_digval(unsigned c){
    for(int i=0;i<18;i++){ unsigned d = (unsigned short)(c - DBLK[i]); if(d<=9) return (int)d; }
    return -1;
}
static int ref_isws(unsigned c){
    for(int i=0;i<26;i++) if(c==WSET[i]) return 1;
    return 0;
}

int ref_wtoi(const unsigned short* s){
    const unsigned short* p = s;
    while(ref_isws(*p)) ++p;
    int neg = 0;
    if(*p=='-'){ neg=1; ++p; } else if(*p=='+'){ ++p; }
    unsigned long long acc = 0;
    for(;;){
        int d = ref_digval(*p);
        if(d<0) break;
        ++p;
        acc = acc*10 + (unsigned)d;
        if(acc >= 0x100000000ULL) acc = 0x100000000ULL;   /* saturating cap, as in change 108 */
    }
    if(neg) return (acc >= 0x80000000ULL) ? (int)0x80000000 : -(int)acc;
    return (acc > 0x7FFFFFFFULL) ? 0x7FFFFFFF : (int)acc;
}
