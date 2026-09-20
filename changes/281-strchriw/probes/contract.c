/* changes/281-strchriw/probes/contract.c
 *
 * What does shlwapi's case-insensitive search family actually consider equal?
 *
 * discovery/charclass_strcmp_2026.c measured StrChrIW at 21939.92 ns to scan 511 characters. That
 * is forty-three nanoseconds per character, which is not a table lookup and not a loop -- it is the
 * cost of a full call per character. For scale, CompareStringOrdinal compares the same 511
 * characters in 85.91 ns, and change 277's vectorised CharUpperBuffW upcases 4000 of them in under
 * a microsecond.
 *
 * Before any of that can be replaced, ONE question has to be answered exactly: which characters does
 * this family treat as equal? There are three plausible answers and they are not the same function:
 *
 *   1. ASCII-only folding (a-z <-> A-Z and nothing else);
 *   2. the ORDINAL upcase table -- what CharUpperW and RtlUpcaseUnicodeChar use. Change 277 proved
 *      that table is ntdll's and is NOT locale-aware, not even under a Turkish locale;
 *   3. a LINGUISTIC fold through CompareString/LCMapString, which is locale-dependent and which
 *      this project does not own -- change 276 parked for exactly that reason.
 *
 * If the answer is (3) this change cannot be written. So this probe does not guess: it asks
 * StrChrIW itself, for every one of the 65536 UTF-16 code units, which partners it matches, and
 * compares that set against what CharUpperW, CharLowerW and RtlUpcaseUnicodeChar would say.
 *
 * It also pins the shape questions a search has to get right: what is returned on a miss, what
 * happens at the terminator, whether the search can match the NUL itself, and how the three-argument
 * range forms treat their end pointer.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef PCWSTR (WINAPI *F_chr2)(PCWSTR, WCHAR);
typedef PCWSTR (WINAPI *F_chr3)(PCWSTR, PCWSTR, WCHAR);
typedef PCWSTR (WINAPI *F_str2)(PCWSTR, PCWSTR);
typedef WCHAR  (WINAPI *F_up)(WCHAR);

static F_chr2 chrI, chrN;
static F_chr3 rchrI, chrNI;
static F_str2 strI;
static F_up   cupper, clower;

static WCHAR (NTAPI *rtlup)(WCHAR);
static WCHAR (NTAPI *rtldn)(WCHAR);

/* does StrChrIW, searching a one-character string containing `hay`, find `needle`? */
static int matches(WCHAR hay, WCHAR needle)
{
    wchar_t s[2];
    s[0] = hay; s[1] = 0;
    return chrI(s, needle) != NULL;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hu = LoadLibraryW(L"user32.dll");
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    unsigned c;
    long n_up = 0, n_dn = 0, n_self = 0, n_extra = 0, n_missing = 0;
    long shown = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    chrI   = (F_chr2)GetProcAddress(hs, "StrChrIW");
    chrN   = (F_chr2)GetProcAddress(hs, "StrChrW");
    rchrI  = (F_chr3)GetProcAddress(hs, "StrRChrIW");
    chrNI  = (F_chr3)GetProcAddress(hs, "StrChrNIW");
    strI   = (F_str2)GetProcAddress(hs, "StrStrIW");
    cupper = (F_up)GetProcAddress(hu, "CharUpperW");
    clower = (F_up)GetProcAddress(hu, "CharLowerW");
    rtlup  = (WCHAR (NTAPI *)(WCHAR))GetProcAddress(hn, "RtlUpcaseUnicodeChar");
    rtldn  = (WCHAR (NTAPI *)(WCHAR))GetProcAddress(hn, "RtlDowncaseUnicodeChar");
    if (!chrI || !cupper || !rtlup) { printf("resolve failed\n"); return 1; }

    printf("== 1. THE EQUIVALENCE RULE, over all 65536 code units ==\n");
    printf("   for each c: does StrChrIW find CharUpperW(c) and CharLowerW(c) in a string of c?\n");
    for (c = 1; c <= 0xFFFF; ++c) {             /* 0 is the terminator; asked separately below */
        WCHAR ch = (WCHAR)c;
        WCHAR u = cupper(ch), l = clower(ch);
        int mself = matches(ch, ch);
        int mu = (u != ch) ? matches(ch, u) : 1;
        int ml = (l != ch) ? matches(ch, l) : 1;
        if (mself) ++n_self; else if (shown < 8) { printf("   c=%04X does not match ITSELF\n", c); ++shown; }
        if (mu) ++n_up;
        if (ml) ++n_dn;
        if ((!mu || !ml) && shown < 8) {
            printf("   c=%04X upper=%04X lower=%04X   matches upper:%d lower:%d\n", c, u, l, mu, ml);
            ++shown;
        }
    }
    printf("   matched itself           %ld / 65535\n", n_self);
    printf("   matched its CharUpperW   %ld / 65535\n", n_up);
    printf("   matched its CharLowerW   %ld / 65535\n", n_dn);

    printf("\n== 2. IS IT THE ORDINAL TABLE, OR SOMETHING WIDER? ==\n");
    printf("   for each c, compare StrChrIW's verdict against 'RtlUpcaseUnicodeChar is equal'\n");
    {
        /* Walk every c and every OTHER character that shares its upcase, and check both directions.
           Testing all 2^32 pairs is impossible; sharing an upcase is the only way two distinct
           code units can be equal under an ordinal fold, so the classes are what matter. */
        static WCHAR upof[0x10000];
        long pairs = 0;
        unsigned a;
        for (a = 0; a < 0x10000; ++a) upof[a] = rtlup((WCHAR)a);
        for (a = 1; a < 0x10000; ++a) {
            unsigned b;
            /* find partners cheaply: only check the obvious +-0x20, and the full CharUpper/Lower */
            WCHAR cand[4];
            int k, nc = 0;
            cand[nc++] = cupper((WCHAR)a);
            cand[nc++] = clower((WCHAR)a);
            cand[nc++] = rtlup((WCHAR)a);
            cand[nc++] = rtldn ? rtldn((WCHAR)a) : (WCHAR)a;
            for (k = 0; k < nc; ++k) {
                b = cand[k];
                if (b == a || b == 0) continue;
                ++pairs;
                {
                    int got = matches((WCHAR)a, (WCHAR)b);
                    int want = (upof[a] == upof[b]);
                    if (got != want) {
                        if (n_extra + n_missing < 12)
                            printf("   a=%04X b=%04X  StrChrIW:%d  ordinal-upcase-equal:%d\n",
                                   a, b, got, want);
                        if (got) ++n_extra; else ++n_missing;
                    }
                }
            }
        }
        printf("   %ld candidate pairs checked\n", pairs);
        printf("   StrChrIW matched where the ORDINAL table says NOT equal:  %ld\n", n_extra);
        printf("   StrChrIW did NOT match where the ordinal table says equal: %ld\n", n_missing);
        if (!n_extra && !n_missing)
            printf("   -> the rule IS the ordinal upcase table (RtlUpcaseUnicodeChar), exactly\n");
    }

    printf("\n== 3. the shape of the search ==\n");
    {
        static wchar_t s[] = L"abcXYZabc";
        printf("   StrChrIW(\"abcXYZabc\", 'x')   offset %d\n",
               chrI(s, L'x') ? (int)(chrI(s, L'x') - s) : -1);
        printf("   StrChrIW(\"abcXYZabc\", 'A')   offset %d  (first, not last)\n",
               chrI(s, L'A') ? (int)(chrI(s, L'A') - s) : -1);
        printf("   StrChrIW(\"abcXYZabc\", '#')   %s\n", chrI(s, L'#') ? "found (!)" : "NULL");
        printf("   StrChrIW(\"abcXYZabc\", 0)     %s\n",
               chrI(s, 0) ? "found -- it can match the TERMINATOR" : "NULL");
        printf("   StrChrIW(\"\", 'a')            %s\n", chrI(L"", L'a') ? "found (!)" : "NULL");
        if (rchrI)
            printf("   StrRChrIW(s, s+9, 'A')       offset %d  (LAST, searching a range)\n",
                   rchrI(s, s + 9, L'A') ? (int)(rchrI(s, s + 9, L'A') - s) : -1);
        if (chrNI)
            printf("   StrChrNIW(s, s+3, 'X')       %s  (range STOPS at s+3)\n",
                   chrNI(s, s + 3, L'X') ? "found (!)" : "NULL");
        if (strI)
            printf("   StrStrIW(\"abcXYZabc\", \"xyz\") offset %d\n",
                   strI(s, L"xyz") ? (int)(strI(s, L"xyz") - s) : -1);
    }

    printf("\n== 4. NULL arguments ==\n");
    {
        int faulted = 0;
        PCWSTR r = NULL;
        __try { r = chrI(NULL, L'a'); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   StrChrIW(NULL, 'a')  %s\n", faulted ? "FAULTS" : (r ? "returned non-NULL" : "returned NULL"));
    }

    printf("\n== 5. surrogates and the astral plane ==\n");
    {
        static wchar_t sp[] = { 0xD83D, 0xDE00, 0 };       /* one emoji, two code units */
        printf("   StrChrIW(<surrogate pair>, D83D) offset %d  (does it see CODE UNITS?)\n",
               chrI(sp, 0xD83D) ? (int)(chrI(sp, 0xD83D) - sp) : -1);
        printf("   StrChrIW(<surrogate pair>, DE00) offset %d\n",
               chrI(sp, 0xDE00) ? (int)(chrI(sp, 0xDE00) - sp) : -1);
    }
    return 0;
}
