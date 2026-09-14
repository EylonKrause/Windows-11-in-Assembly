/* Derive ucrtbase!_wtoi64, then fuzz a candidate reference against the live export.

   The wide whitespace/digit/sign sets were settled in change 186 (18 contiguous blocks of ten,
   26 whitespace units, U+002D/U+002B only, locale-independent across 8 locales). What is NOT
   inherited here is the 64-bit RESULT behaviour: change 109 measured that the byte form `_atoi64`
   saturates at _I64_MAX / _I64_MIN, and the byte and wide forms of other CRT families have
   diverged before, so it is re-measured.

   Unknowns to pin:
     1. overflow: saturate at _I64_MAX / _I64_MIN, or wrap?
     2. is the negative limit exactly _I64_MIN (i.e. is a magnitude of 2^63 accepted)?
     3. does it stop at the first non-digit like _wtoi?
   Build: cl /nologo /O2 /MD wtoi64.c && wtoi64.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef __int64 (__cdecl *W64)(const wchar_t*);
static W64 w64;

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };
static int digval(unsigned c){
    for(int i=0;i<18;i++){ unsigned d=(unsigned short)(c-DBLK[i]); if(d<=9) return (int)d; }
    return -1; }
static int isws(unsigned c){ for(int i=0;i<26;i++) if(c==WSET[i]) return 1; return 0; }

/* candidate: change 109's saturating 64-bit body with change 186's wide sets */
static __int64 ref_wtoi64(const wchar_t* s){
    const unsigned short* p = (const unsigned short*)s;
    while(isws(*p)) ++p;
    int neg=0;
    if(*p=='-'){ neg=1; ++p; } else if(*p=='+'){ ++p; }
    unsigned __int64 acc=0;
    const unsigned __int64 DIVCAP = 0x1999999999999999ULL;   /* floor((2^64-1)/10) */
    const unsigned __int64 CAP    = 0x8000000000000000ULL;   /* 2^63 */
    for(;;){
        int d = digval(*p);
        if(d<0) break;
        ++p;
        if(acc>=DIVCAP){ acc=CAP; break; }
        acc = acc*10 + (unsigned)d;
        if(acc>=CAP){ acc=CAP; break; }
    }
    if(neg) return (__int64)(0ULL - acc);                     /* 2^63 negated = _I64_MIN */
    return (acc<CAP) ? (__int64)acc : (__int64)0x7FFFFFFFFFFFFFFFLL;
}

static void show(const wchar_t* s, const char* what){
    __int64 a=w64(s), b=ref_wtoi64(s);
    printf("  %-40s -> %21lld   (ref %21lld)%s\n", what, a, b, a==b?"":"  <== MISMATCH");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    w64 = (W64)GetProcAddress(hu,"_wtoi64");
    if(!w64){ printf("missing export\n"); return 1; }

    printf("== 1. 64-bit overflow: saturate or wrap? ==\n");
    show(L"9223372036854775806", "_I64_MAX - 1");
    show(L"9223372036854775807", "_I64_MAX");
    show(L"9223372036854775808", "_I64_MAX + 1");
    show(L"9223372036854775809", "_I64_MAX + 2");
    show(L"18446744073709551615", "2^64 - 1");
    show(L"18446744073709551616", "2^64");
    show(L"99999999999999999999999999", "26 nines");
    show(L"-9223372036854775807", "-(_I64_MAX)");
    show(L"-9223372036854775808", "_I64_MIN exactly");
    show(L"-9223372036854775809", "_I64_MIN - 1");
    show(L"-99999999999999999999999999", "-26 nines");

    printf("\n== 2. structure (should mirror _wtoi) ==\n");
    show(L"  -42", "whitespace then minus");
    show(L"- 42", "minus then whitespace");
    show(L"", "empty");
    show(L"+", "plus alone");
    show(L"0000000000000000000000042", "23 leading zeros");
    show(L"42abc", "digits then letters");

    printf("\n== 3. non-ASCII blocks reach the same 64-bit path ==\n");
    { static wchar_t b[48]; const wchar_t* d=L"9223372036854775808"; int k=0;
      b[k++]=L'-'; for(int i=0;d[i];i++) b[k++]=(wchar_t)(0x0660+(d[i]-L'0')); b[k]=0;
      show(b, "_I64_MIN in Arabic-Indic digits"); }
    { static wchar_t b[48]; const wchar_t* d=L"9223372036854775808"; int k=0;
      for(int i=0;d[i];i++) b[k++]=(wchar_t)(0xFF10+(d[i]-L'0')); b[k]=0;
      show(b, "_I64_MAX+1 in fullwidth digits"); }

    printf("\n== 4. fuzz the candidate reference against the live export ==\n");
    {
        unsigned long sd=9931;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static wchar_t buf[48];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int len = 1 + (int)(RND%26);            /* long enough to cross 2^63 often */
            for(int i=0;i<len;i++){
                unsigned r = RND%100;
                if(r<60)      buf[i]=(wchar_t)(L'0'+(RND%10));
                else if(r<72) buf[i]=(wchar_t)(DBLK[RND%18]+(RND%10));
                else if(r<82) buf[i]=(wchar_t)WSET[RND%26];
                else if(r<90) buf[i]=(RND%2)?L'-':L'+';
                else if(r<96){ unsigned b=DBLK[RND%18]; buf[i]=(wchar_t)(b-1+(RND%12)); }
                else          buf[i]=(wchar_t)(1+(RND%0xFFFE));
            }
            buf[len]=0;
            __int64 a=w64(buf), b=ref_wtoi64(buf);
            if(a!=b){ if(bad<8){ printf("    MISMATCH live=%lld ref=%lld  [",a,b);
                                 for(int i=0;i<len;i++) printf("%04X ",buf[i]); printf("]\n"); }
                      ++bad; }
        }
        printf("  %lld mismatches of %lld  %s\n", bad, N, bad?"RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
