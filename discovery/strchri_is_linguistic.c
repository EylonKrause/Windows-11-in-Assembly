/* discovery/strchri_is_linguistic.c
   RESULT: shlwapi!StrChrIW is NOT a viable target. Kept as the evidence.

   It was the most extreme anomaly in the shlwapi survey: 0.058 GB/s, a flat 34.7 ns PER CHARACTER at
   every length from 8 to 4000, against StrChrW's 5.1 GB/s. Ninety times slower to find a character
   case-insensitively than case-sensitively.

   And at first it looked reproducible. Its fold matches the OS upcase table EXACTLY -- 973 pairs,
   0 missed -- and it is LOCALE-INDEPENDENT across en-US, tr-TR, de-DE, lt-LT, az-Latn-AZ, ru-RU,
   el-GR and ja-JP, including the dotted/dotless-I pairs that normally expose locale dependence. That
   is the same shape change 051 already reproduced for RtlFindCharInUnicodeString.

   But it ALSO folds 973 pairs the upcase table does not relate at all, such as U+0A31 <-> U+D7C8.
   The decisive measurement is the size of each equivalence class:

       a, A                                    match    4 of 65534
       U+00E9, U+0430                          match    2
       U+0A31, U+0EA4, U+2FDC, U+D7C8, ...     match 3236

   A table fold gives tiny classes. A class of 3236 is the IGNORABLE/UNASSIGNED set: those code points
   collate as nothing, so one-character strings containing any two of them compare EQUAL. That is a
   linguistic comparison per character -- which is both why it costs 34.7 ns each and why it cannot be
   reimplemented without the NLS tables.

   The relation is symmetric (0 asymmetric pairs over 200k), and a string of only ignorables still
   does not match an ordinary character, so it really is per-character CompareString semantics rather
   than anything simpler.

   Two files, merged: the semantics/locale probe first, then the equivalence-class test that settled
   it.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef wchar_t* (WINAPI *FN)(const wchar_t*, wchar_t);
typedef WCHAR    (NTAPI  *FN_UP)(WCHAR);
static FN chri, chr;
static FN_UP upcase;

/* does StrChrIW consider a and b the same character? */
static int same(wchar_t a, wchar_t b){
    wchar_t buf[2]; buf[0] = a; buf[1] = 0;
    return chri(buf, b) == buf;
}

int main(void){
    extern int ignorable_class_test(void);
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    chri = (FN)GetProcAddress(h,"StrChrIW");
    chr  = (FN)GetProcAddress(h,"StrChrW");
    upcase = (FN_UP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlUpcaseUnicodeChar");
    if(!chri){ printf("no StrChrIW\n"); return 1; }
    printf("StrChrIW=%p  StrChrW=%p  RtlUpcaseUnicodeChar=%p\n\n",
           (void*)chri, (void*)chr, (void*)upcase);

    printf("=== basics: return value and the not-found case ===\n");
    {
        static const wchar_t S[] = L"Hello World";
        printf("  StrChrIW(\"Hello World\", 'h')  -> offset %lld\n",
               chri(S,L'h') ? (long long)(chri(S,L'h') - S) : -1LL);
        printf("  StrChrIW(\"Hello World\", 'H')  -> offset %lld\n",
               chri(S,L'H') ? (long long)(chri(S,L'H') - S) : -1LL);
        printf("  StrChrIW(\"Hello World\", 'w')  -> offset %lld\n",
               chri(S,L'w') ? (long long)(chri(S,L'w') - S) : -1LL);
        printf("  StrChrIW(\"Hello World\", 'z')  -> %s\n", chri(S,L'z') ? "found?!" : "NULL");
        printf("  StrChrIW(\"Hello World\", 0)    -> offset %lld (the terminator?)\n",
               chri(S,0) ? (long long)(chri(S,0) - S) : -1LL);
        printf("  StrChrIW(\"\", 'a')             -> %s\n", chri(L"",L'a') ? "found?!" : "NULL");
        printf("  StrChrIW(\"\", 0)               -> %s\n", chri(L"",0) ? "found" : "NULL");
    }

    printf("\n=== which pairs fold, and does it match the OS upcase table? ===\n");
    {
        int folds = 0, mismatch_up = 0, examples = 0, nonascii = 0;
        for (unsigned c = 1; c < 0xFFFF; ++c) {
            wchar_t up = upcase ? upcase((WCHAR)c) : (wchar_t)c;
            if (up == (wchar_t)c) continue;
            int f = same((wchar_t)c, up);          /* does the string char c match the search char up */
            if (f) { ++folds; if (c > 0x7F) ++nonascii; }
            else {
                ++mismatch_up;
                if (examples < 10) { printf("    upcase pair NOT folded by StrChrIW: U+%04X -> U+%04X\n",
                                            c, up); ++examples; }
            }
        }
        printf("  pairs the OS upcase table defines that StrChrIW folds: %d (%d of them above ASCII)\n",
               folds, nonascii);
        printf("  upcase pairs StrChrIW does NOT fold: %d\n", mismatch_up);
    }

    printf("\n=== does StrChrIW fold anything the upcase table does NOT? ===\n");
    {
        int extra = 0, examples = 0;
        /* sample pairs (c, d) that are NOT upcase-related and check they do not match */
        unsigned long sd = 0x2099u;
        for (int t = 0; t < 400000; ++t) {
            sd = sd*1103515245u + 12345u; wchar_t a = (wchar_t)(1 + (sd>>13) % 0xFFFE);
            sd = sd*1103515245u + 12345u; wchar_t b = (wchar_t)(1 + (sd>>13) % 0xFFFE);
            if (a == b) continue;
            wchar_t ua = upcase ? upcase(a) : a, ub = upcase ? upcase(b) : b;
            if (ua == ub) continue;                 /* legitimately the same under upcase */
            if (same(a, b)) {
                ++extra;
                if (examples < 10) { printf("    UNEXPECTED fold: U+%04X <-> U+%04X (upcase %04X vs %04X)\n",
                                            a, b, ua, ub); ++examples; }
            }
        }
        printf("  unexpected folds over 400k random pairs: %d\n", extra);
    }

    printf("\n=== is it LOCALE-INDEPENDENT? ===\n");
    {
        static const wchar_t* NAMES[] = { L"en-US", L"tr-TR", L"de-DE", L"lt-LT", L"az-Latn-AZ",
                                          L"ru-RU", L"el-GR", L"ja-JP" };
        /* Turkish and Lithuanian are the classic case-folding divergences (dotted/dotless I) */
        static const wchar_t PROBE[][2] = {
            { L'i', L'I' }, { L'I', L'i' }, { 0x0130, L'i' }, { 0x0131, L'I' },
            { 0x00E9, 0x00C9 }, { 0x03C3, 0x03A3 }, { 0x0430, 0x0410 }
        };
        for (int L = 0; L < 8; ++L) {
            ULONG n = 0; LCID lcid = 0;
            if (!LocaleNameToLCID(NAMES[L], 0)) { printf("  %-12ls (unavailable)\n", NAMES[L]); continue; }
            lcid = LocaleNameToLCID(NAMES[L], 0);
            SetThreadLocale(lcid);
            printf("  %-12ls ", NAMES[L]);
            for (int k = 0; k < 7; ++k) printf("%d", same(PROBE[k][0], PROBE[k][1]));
            printf("   (i/I, I/i, U+0130/i, U+0131/I, e-acute, sigma, cyrillic-a)\n");
            (void)n;
        }
        SetThreadLocale(LocaleNameToLCID(L"en-US", 0));
    }

    printf("\n=== cost, again, at several lengths ===\n");
    {
        LARGE_INTEGER F,s,e; QueryPerformanceFrequency(&F);
        SetThreadAffinityMask(GetCurrentThread(),(DWORD_PTR)1<<2);
        SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
        static wchar_t BUF[8192];
        for (int i = 0; i < 8000; ++i) BUF[i] = (wchar_t)(L'a' + (i % 26));
        BUF[8000] = 0;
        const int LENS[] = { 8, 64, 512, 4000 };
        volatile uint64_t sink = 0;
        for (int k = 0; k < 4; ++k) {
            wchar_t save = BUF[LENS[k]]; BUF[LENS[k]] = 0;
            int N = (LENS[k] > 512) ? 20000 : 200000;
            double best = 1e300;
            for (int t = 0; t < 5; ++t) {
                QueryPerformanceCounter(&s);
                for (int i = 0; i < N; ++i) sink ^= (uint64_t)(size_t)chri(BUF, L'#');
                QueryPerformanceCounter(&e);
                double ns = (double)(e.QuadPart-s.QuadPart)*1e9/(double)F.QuadPart/N;
                if (ns < best) best = ns;
            }
            printf("  StrChrIW miss over %5d wchar: %10.2f ns  (%6.3f GB/s)\n",
                   LENS[k], best, (LENS[k]*2.0)/best);
            best = 1e300;
            for (int t = 0; t < 5; ++t) {
                QueryPerformanceCounter(&s);
                for (int i = 0; i < N; ++i) sink ^= (uint64_t)(size_t)chr(BUF, L'#');
                QueryPerformanceCounter(&e);
                double ns = (double)(e.QuadPart-s.QuadPart)*1e9/(double)F.QuadPart/N;
                if (ns < best) best = ns;
            }
            printf("  StrChrW  miss over %5d wchar: %10.2f ns  (%6.3f GB/s)\n",
                   LENS[k], best, (LENS[k]*2.0)/best);
            BUF[LENS[k]] = save;
        }
        printf("  sink=%llu\n", (unsigned long long)sink);
    }
    return 0;
}


/* ==================== the equivalence-class test that settled it ==================== */

typedef wchar_t* (WINAPI *FN)(const wchar_t*, wchar_t);
typedef WCHAR    (NTAPI  *FN_UP)(WCHAR);
static FN chri;
static FN_UP upcase;

static int same(wchar_t a, wchar_t b){
    wchar_t buf[2]; buf[0] = a; buf[1] = 0;
    return chri(buf, b) == buf;
}

int ignorable_class_test(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    chri = (FN)GetProcAddress(h,"StrChrIW");
    upcase = (FN_UP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlUpcaseUnicodeChar");
    if(!chri){ printf("no StrChrIW\n"); return 1; }

    printf("=== how many characters does each 'unexpected' partner match? ===\n");
    printf("A table fold gives tiny equivalence classes (2, rarely 3). An IGNORABLE character\n");
    printf("matches every other ignorable, so its class is large.\n\n");
    {
        static const wchar_t SUSPECT[] = { 0x0A31, 0x0EA4, 0x2FDC, 0xD7C8, 0xDAF9, 0x2B79,
                                           L'a', L'A', 0x00E9, 0x0430 };
        for (int k = 0; k < 10; ++k) {
            wchar_t c = SUSPECT[k];
            int cls = 0;
            for (unsigned d = 1; d < 0xFFFF; ++d) if (same(c, (wchar_t)d)) ++cls;
            printf("  U+%04X matches %6d of 65534 characters   (upcase -> U+%04X)\n",
                   c, cls, upcase ? upcase(c) : c);
        }
    }

    printf("\n=== is the relation even an equivalence? (a~b but not b~a would settle it) ===\n");
    {
        int asym = 0, shown = 0;
        unsigned long sd = 0x1234u;
        for (int t = 0; t < 200000; ++t) {
            sd = sd*1103515245u+12345u; wchar_t a = (wchar_t)(1 + (sd>>13) % 0xFFFE);
            sd = sd*1103515245u+12345u; wchar_t b = (wchar_t)(1 + (sd>>13) % 0xFFFE);
            int ab = same(a,b), ba = same(b,a);
            if (ab != ba) { ++asym; if (shown < 8) { printf("  ASYMMETRIC: U+%04X~U+%04X=%d but reverse=%d\n",
                                                            a,b,ab,ba); ++shown; } }
        }
        printf("  asymmetric pairs over 200k: %d\n", asym);
    }

    printf("\n=== does a string of ignorables match anything? ===\n");
    {
        wchar_t buf[4]; buf[0] = 0x0A31; buf[1] = 0x0EA4; buf[2] = 0x2FDC; buf[3] = 0;
        printf("  StrChrIW(L\"\\u0A31\\u0EA4\\u2FDC\", 'x') -> %s\n", chri(buf,L'x') ? "FOUND" : "NULL");
        printf("  StrChrIW(L\"\\u0A31\\u0EA4\\u2FDC\", 'a') -> %s\n", chri(buf,L'a') ? "FOUND" : "NULL");
        wchar_t mix[4]; mix[0] = L'a'; mix[1] = 0x0A31; mix[2] = L'b'; mix[3] = 0;
        printf("  StrChrIW(L\"a\\u0A31b\", 'B')            -> offset %lld\n",
               chri(mix,L'B') ? (long long)(chri(mix,L'B') - mix) : -1LL);
    }
    return 0;
}
