// changes/188-wcstol/reference.c
// The correctness oracle: the obvious scalar wcstol. Not fast; just correct.
//
// Contract derived in probes/wcstol.c and fuzz-confirmed against the live export -- value,
// *endptr AND errno -- over 1,500,000 cases, 0 mismatches. It is change 110's strtol structure
// crossed with change 186's wide sets, and the crossing is where the surprises live:
//   * non-ASCII digits work in EVERY base, subject to the ordinary d >= base rejection;
//   * the base>10 letters are ASCII-ONLY (fullwidth 'f' is not a hex digit, though fullwidth '9'
//     IS a decimal one);
//   * the "0x" prefix zero, and base-0 octal detection, accept ANY BLOCK'S ZERO -- the variant
//     that required the ASCII L'0' was refuted on 1579 of 1,500,000;
//   * but the 'x' itself is ASCII-only;
//   * an invalid base raises the invalid-parameter handler, sets EINVAL, writes *endptr = nptr
//     and returns 0.
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

/* value 0..35 for a base up to 36, or -1. The 18 blocks give 0..9; letters are ASCII-only. */
int ref_dv36(unsigned c){
    for(int i=0;i<18;i++){ unsigned d=(unsigned short)(c-DBLK[i]); if(d<=9) return (int)d; }
    if(c>='a'&&c<='z') return (int)(c-'a'+10);
    if(c>='A'&&c<='Z') return (int)(c-'A'+10);
    return -1;
}

/* Set by the oracle when the base is invalid, so the test can compare handler behaviour too. */
int ref_wcstol_badbase = 0;

long ref_wcstol(const unsigned short* nptr, unsigned short** endptr, int base){
    ref_wcstol_badbase = 0;
    if(base != 0 && (base < 2 || base > 36)){
        ref_wcstol_badbase = 1;
        if(endptr) *endptr = (unsigned short*)nptr;
        errno = EINVAL;
        return 0;
    }
    const unsigned short* s = nptr;
    while(ref_isws(*s)) ++s;
    int neg=0;
    if(*s=='-'){ neg=1; ++s; } else if(*s=='+'){ ++s; }
    if(base==0){ base = (ref_dv36(*s)==0) ? ((s[1]=='x'||s[1]=='X')?16:8) : 10; }
    if(base==16 && ref_dv36(s[0])==0 && (s[1]=='x'||s[1]=='X')) s+=2;
    const unsigned short* digstart=s;
    unsigned long long acc=0; int ovf=0;
    for(;;){
        int d = ref_dv36(*s);
        if(d<0 || d>=base) break;
        if(!ovf){ acc=acc*base+d; if(acc>0x100000000ULL){ acc=0x100000000ULL; ovf=1; } }
        ++s;
    }
    if(s==digstart){ if(endptr)*endptr=(unsigned short*)nptr; return 0; }
    if(endptr)*endptr=(unsigned short*)s;
    unsigned long long limit = neg?2147483648ULL:2147483647ULL;
    if(acc>limit){ errno=ERANGE; return neg?(long)0x80000000:(long)0x7FFFFFFF; }
    return neg?-(long)acc:(long)acc;
}
