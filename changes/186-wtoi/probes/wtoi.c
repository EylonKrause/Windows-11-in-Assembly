/* Derive ucrtbase!_wtoi (== _wtol, same export address), then fuzz a candidate reference
   against the live export.

   THIS FUNCTION WAS EXPLICITLY SCOPED OUT OF THIS REPO. changes/109-atoi64/RESULTS.md:
     "The wide sibling _wtoi is not included: ucrtbase's _wtoi recognizes Unicode decimal digits
      (Arabic-Indic U+0660-0669, fullwidth U+FF10-FF19, etc.) by their digit value, so a
      bit-exact reimpl would need the CRT's full Unicode digit table, not an ASCII loop."
   That objection is now answered by measurement rather than by assumption. Two sweeps settled it:

     * DIGITS, probing L"2" + c + L"1" over all 65536 code units (the middle position removes
       the leading-whitespace, sign and digit-zero confounds) the accepted set is EXACTLY
       18 CONTIGUOUS BLOCKS OF TEN, every block ascending 0..9:
         0030 0660 06F0 0966 09E6 0A66 0AE6 0B66 0C66 0CE6
         0D66 0E50 0ED0 0F20 1040 17E0 1810 FF10
       180 members, 18 runs, no exceptions. That is the Unicode 3.0-era Nd set -- the same list
       change 166 found frozen in ntdll, independently confirmed here in a different DLL.
     * LOCALE; the sets are byte-identical under LC_ALL = C, en-US, ar-SA, ja-JP, th-TH, hi-IN,
       de-DE.UTF-8 and .65001. Not locale-sensitive, so a fixed table is honest.
     * WHITESPACE, 26 code units: 0009-000D, 0020, 0085, 00A0, 1680, 180E, 2000-200A, 2028,
       2029, 202F, 205F, 3000. (U+200B is NOT included.) The byte form (change 108) skips only
       six, so this is the other place the wide form genuinely differs.
     * SIGN, exactly U+002D and U+002B, no Unicode minus/plus variants.

   What is left to pin HERE:
     1. overflow: does it saturate like atoi (INT_MAX / INT_MIN), or wrap?
     2. is a sign accepted only once, and only immediately after the whitespace run?
     3. do digits from DIFFERENT blocks concatenate, or does a block switch stop the parse?
     4. empty / whitespace-only / sign-only input.
   Build: cl /nologo /O2 /MD wtoi.c && wtoi.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (__cdecl *WTOI)(const wchar_t*);
static WTOI wtoi, wtol;

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };

static int digval(unsigned c){
    for(int i=0;i<18;i++){ unsigned d = (unsigned short)(c - DBLK[i]); if(d<=9) return (int)d; }
    return -1;
}
static int isws(unsigned c){
    for(int i=0;i<26;i++) if(c==WSET[i]) return 1;
    return 0;
}

/* candidate reference: atoi's shape (change 108) with the wide whitespace and digit sets */
static int ref_wtoi(const wchar_t* s){
    const unsigned short* p = (const unsigned short*)s;
    while(isws(*p)) ++p;
    int neg = 0;
    if(*p=='-'){ neg=1; ++p; } else if(*p=='+'){ ++p; }
    unsigned long long acc = 0;
    for(;;){
        int d = digval(*p);
        if(d<0) break;
        ++p;
        acc = acc*10 + (unsigned)d;
        if(acc >= 0x100000000ULL){ acc = 0x100000000ULL; }   /* saturating cap, as in change 108 */
    }
    if(neg) return (acc >= 0x80000000ULL) ? (int)0x80000000 : -(int)acc;
    return (acc > 0x7FFFFFFFULL) ? 0x7FFFFFFF : (int)acc;
}

static void show(const wchar_t* s, const char* what){
    printf("  %-44s -> %12d   (ref %12d) %s\n", what, wtoi(s), ref_wtoi(s),
           wtoi(s)==ref_wtoi(s)?"":"  <== MISMATCH");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    wtoi = (WTOI)GetProcAddress(hu,"_wtoi");
    wtol = (WTOI)GetProcAddress(hu,"_wtol");
    if(!wtoi||!wtol){ printf("missing export\n"); return 1; }
    printf("_wtoi @ %p, _wtol @ %p  -> %s\n\n", (void*)wtoi, (void*)wtol,
           wtoi==wtol ? "SAME CODE (one implementation covers both names)" : "different");

    printf("== 1. overflow: saturate or wrap? ==\n");
    show(L"2147483647",   "INT_MAX exactly");
    show(L"2147483648",   "INT_MAX + 1");
    show(L"4294967296",   "2^32");
    show(L"99999999999999999999", "20 nines");
    show(L"-2147483648",  "INT_MIN exactly");
    show(L"-2147483649",  "INT_MIN - 1");
    show(L"-99999999999999999999", "-20 nines");

    printf("\n== 2. sign placement ==\n");
    show(L"  -42",  "whitespace then minus");
    show(L"- 42",   "minus then whitespace");
    show(L"--42",   "double minus");
    show(L"+-42",   "plus then minus");
    show(L"+42",    "plus");
    show(L"-",      "minus alone");
    show(L"",       "empty");
    show(L"      ", "whitespace only");
    show(L"abc",    "no digits");

    printf("\n== 3. do digits from DIFFERENT blocks concatenate? ==\n");
    { static wchar_t b[8]; b[0]=0x0661; b[1]=0xFF12; b[2]=0x0033; b[3]=0;   /* 1 2 3 mixed */
      show(b, "U+0661 U+FF12 '3'  (three blocks)"); }
    { static wchar_t b[8]; b[0]=0x0031; b[1]=0x0660; b[2]=0x0031; b[3]=0;
      show(b, "'1' U+0660 '1'"); }
    { static wchar_t b[8]; b[0]=0x0031; b[1]=0x200B; b[2]=0x0031; b[3]=0;
      show(b, "'1' U+200B '1'  (ZWSP is NOT whitespace)"); }

    printf("\n== 4. every whitespace code unit is actually skipped ==\n");
    { int bad=0;
      for(int i=0;i<26;i++){ wchar_t b[8]; b[0]=WSET[i]; b[1]=L'-'; b[2]=L'7'; b[3]=0;
                             if(wtoi(b)!=-7) { printf("    U+%04X NOT skipped\n",WSET[i]); ++bad; } }
      printf("    %d of 26 failed\n", bad); }

    printf("\n== 5. every digit block contributes its value ==\n");
    { int bad=0;
      for(int i=0;i<18;i++) for(int d=0;d<10;d++){
          wchar_t b[8]; b[0]=(wchar_t)(DBLK[i]+d); b[1]=L'5'; b[2]=0;
          if(wtoi(b)!=d*10+5){ printf("    U+%04X gave %d\n", DBLK[i]+d, wtoi(b)); ++bad; } }
      printf("    %d of 180 failed\n", bad); }

    printf("\n== 6. fuzz the candidate reference against the live export ==\n");
    {
        unsigned long sd=8611;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static wchar_t buf[40];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int len = 1 + (int)(RND%18);
            for(int i=0;i<len;i++){
                unsigned r = RND%100;
                if(r<55){                                   /* an ASCII digit */
                    buf[i]=(wchar_t)(L'0'+(RND%10));
                } else if(r<70){                            /* a non-ASCII digit */
                    buf[i]=(wchar_t)(DBLK[RND%18]+(RND%10));
                } else if(r<82){                            /* whitespace */
                    buf[i]=(wchar_t)WSET[RND%26];
                } else if(r<90){                            /* a sign */
                    buf[i]=(RND%2)?L'-':L'+';
                } else if(r<96){                            /* a near-miss around a block */
                    unsigned b = DBLK[RND%18];
                    buf[i]=(wchar_t)(b - 1 + (RND%12));
                } else {                                    /* anything at all */
                    buf[i]=(wchar_t)(1+(RND%0xFFFE));
                }
            }
            buf[len]=0;
            int a = wtoi(buf), b = ref_wtoi(buf);
            if(a!=b){
                if(bad<8){ printf("    MISMATCH live=%d ref=%d  [", a, b);
                           for(int i=0;i<len;i++) printf("%04X ", buf[i]); printf("]\n"); }
                ++bad;
            }
        }
        printf("  %lld mismatches of %lld  %s\n", bad, N, bad?"RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
