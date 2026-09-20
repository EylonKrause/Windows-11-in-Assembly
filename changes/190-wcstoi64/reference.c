// changes/190-wcstoi64/reference.c
// The correctness oracle: the obvious scalar _wcstoi64. Not fast; just correct.
//
// _wcstoi64 and wcstoll share one code address, so this oracle covers both names.
// Contract derived in probes/wcstoi64.c and fuzz-confirmed against the live export, value,
// *endptr AND errno, over 1,500,000 cases, 0 mismatches. It is change 188's wide/base crossing
// with change 112's 64-bit signed tail, re-measured rather than inherited:
//   * the overflow limit is SIGN-DEPENDENT (2^63-1 positive, 2^63 negative), so
//     "-9223372036854775808" is EXACT while "9223372036854775808" is ERANGE;
//   * overflow saturates to _I64_MAX / _I64_MIN, with *endptr still past ALL digits;
//   * the "0x" prefix zero may be ANY block's zero; the ASCII-only variant was refuted on
//     1525 of 1,500,000.
#include <stddef.h>
#include <errno.h>

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };
static int ref_isws(unsigned c){ for(int i=0;i<26;i++) if(c==WSET[i]) return 1; return 0; }
static int ref_iszero(unsigned c){ for(int i=0;i<18;i++) if(c==DBLK[i]) return 1; return 0; }

/* value 0..35, or 99 for "not a digit". The 18 blocks give 0..9; letters are ASCII-only. */
int ref_dv36_64(unsigned c){
    for(int i=0;i<18;i++){ unsigned d=(unsigned short)(c-DBLK[i]); if(d<=9) return (int)d; }
    if(c>='a'&&c<='z') return (int)(c-'a'+10);
    if(c>='A'&&c<='Z') return (int)(c-'A'+10);
    return 99;
}

static const unsigned short* ref_pfx(const unsigned short* s,int* neg,int* base){
    while(ref_isws(*s)) ++s;
    *neg=0; if(*s=='-'){ *neg=1; ++s; } else if(*s=='+') ++s;
    if(*base==0) *base = ref_iszero(*s) ? ((s[1]=='x'||s[1]=='X')?16:8) : 10;
    if(*base==16 && ref_iszero(s[0]) && (s[1]=='x'||s[1]=='X')) s+=2;
    return s;
}

__int64 ref_wcstoi64(const unsigned short* nptr, unsigned short** endptr, int base){
    if(base != 0 && (base < 2 || base > 36)){
        if(endptr) *endptr = (unsigned short*)nptr;
        errno = EINVAL;
        return 0;
    }
    int neg; const unsigned short* s = ref_pfx(nptr,&neg,&base);
    const unsigned short* dg=s;
    unsigned __int64 limit = neg?0x8000000000000000ULL:0x7FFFFFFFFFFFFFFFULL;
    unsigned __int64 cutoff=limit/base; int cutlim=(int)(limit%base);
    unsigned __int64 acc=0; int ovf=0;
    for(;;){ int d=ref_dv36_64(*s); if(d>=base) break;
        if(!ovf){ if(acc>cutoff||(acc==cutoff&&d>cutlim)) ovf=1; else acc=acc*base+d; } ++s; }
    if(s==dg){ if(endptr)*endptr=(unsigned short*)nptr; return 0; }
    if(endptr)*endptr=(unsigned short*)s;
    if(ovf){ errno=ERANGE; return neg?(__int64)0x8000000000000000ULL:(__int64)0x7FFFFFFFFFFFFFFFLL; }
    return neg?(__int64)(0ULL-acc):(__int64)acc;
}
