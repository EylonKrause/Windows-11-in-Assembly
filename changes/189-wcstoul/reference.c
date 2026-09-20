// changes/189-wcstoul/reference.c
// The correctness oracle: the obvious scalar wcstoul. Not fast; just correct.
//
// Contract derived in ../188-wcstol/probes/wcstol.c, which fuzzed wcstol AND wcstoul side by side
// against their own live exports (value, *endptr AND errno) 1,500,000 cases each, 0
// mismatches. Everything change 188 established applies here, with change 111's UNSIGNED tail:
//   * a leading '-' is accepted and NEGATES MODULO 2^32, so "-1" returns 4294967295, no error;
//   * the overflow limit is 2^32-1 REGARDLESS of the sign (in 188 the sign moves it);
//   * overflow returns ULONG_MAX and sets errno = ERANGE.
// The same run refuted the "prefix zero must be ASCII L'0'" variant for both functions on 1579
// cases, so that quirk is not specific to the signed form.
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
int ref_wcstoul_badbase = 0;

unsigned long ref_wcstoul(const unsigned short* nptr, unsigned short** endptr, int base){
    ref_wcstoul_badbase = 0;
    if(base != 0 && (base < 2 || base > 36)){
        ref_wcstoul_badbase = 1;
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
    /* the limit does not move with the sign, unlike change 188 */
    if(acc>0xFFFFFFFFULL){ errno=ERANGE; return 0xFFFFFFFFUL; }
    return neg ? (unsigned long)(0u-(unsigned long)acc) : (unsigned long)acc;
}
