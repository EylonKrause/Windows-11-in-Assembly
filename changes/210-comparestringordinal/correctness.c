// changes/210-comparestringordinal/correctness.c
// Gate 1: wia_comparestringordinal must be indistinguishable from kernelbase!CompareStringOrdinal.
//
// Three-way: our assembly + wrapper vs the scalar oracle vs the LIVE export on this PC.
//
// The interesting parts of this contract are the ones a casual implementation gets wrong:
//   * a count of -1 means NUL-terminated, but ANY other count is EXACT -- so an embedded NUL is an
//     ordinary character and the comparison must NOT stop at it. There is a dedicated section;
//   * case-insensitive orders by the UPCASED values, so "a" vs "B" is LESS where the raw code units
//     say GREATER. The sweep below covers every code unit against its own upcase partner;
//   * the vector path folds ASCII only and falls back to the 64K table when a chunk holds anything
//     above 0x7F -- so the corpus deliberately mixes ASCII and non-ASCII, and places the non-ASCII
//     character at every offset within a chunk, which is where a mis-placed guard would show.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_comparestringordinal(const wchar_t*, int, const wchar_t*, int, BOOL);
int ref_comparestringordinal(const wchar_t*, int, const wchar_t*, int, BOOL);
extern void wia_upcase_init(void);
extern unsigned short wia_upcase[65536];
typedef int (WINAPI *FN)(LPCWCH, int, LPCWCH, int, BOOL);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static int one(const wchar_t* a, int ca, const wchar_t* b, int cb, BOOL ic){
    int ra = wia_comparestringordinal(a, ca, b, cb, ic);
    int rb = ref_comparestringordinal(a, ca, b, cb, ic);
    int rc = sys(a, ca, b, cb, ic);
    return (ra == rb && ra == rc);
}
static int both(const wchar_t* a, int ca, const wchar_t* b, int cb){
    return one(a, ca, b, cb, FALSE) && one(a, ca, b, cb, TRUE);
}

static unsigned long sd = 0x210210u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "CompareStringOrdinal");
    if (!sys) { printf("CORRECTNESS: cannot resolve CompareStringOrdinal\n"); return 1; }
    wia_upcase_init();

    // ---- NULL: 0 and ERROR_INVALID_PARAMETER ----
    {
        static const wchar_t A[] = L"abc";
        SetLastError(0); int ra = wia_comparestringordinal(NULL, -1, A, -1, FALSE);
        DWORD ea = GetLastError();
        SetLastError(0); int rc = sys(NULL, -1, A, -1, FALSE);
        DWORD ec = GetLastError();
        CHECK(ra == rc && ra == 0, "NULL first: both return 0");
        CHECK(ea == ec, "NULL first: same last-error");
        CHECK(wia_comparestringordinal(A, -1, NULL, -1, FALSE) == sys(A, -1, NULL, -1, FALSE),
              "NULL second");
    }

    // ---- hand-picked orderings, both modes ----
    {
        CHECK(both(L"a", -1, L"b", -1), "a vs b");
        CHECK(both(L"b", -1, L"a", -1), "b vs a");
        CHECK(both(L"A", -1, L"a", -1), "A vs a  (cs LESS, ci EQUAL)");
        CHECK(both(L"a", -1, L"B", -1), "a vs B  (ci orders by the UPCASED values)");
        CHECK(both(L"Z", -1, L"a", -1), "Z vs a");
        CHECK(both(L"", -1, L"", -1),   "both empty");
        CHECK(both(L"", -1, L"a", -1),  "empty vs a");
        CHECK(both(L"abc", -1, L"abcd", -1), "prefix is LESS");
        CHECK(both(L"abcd", -1, L"abc", -1), "longer is GREATER");
    }

    // ---- explicit counts, including 0 and mismatched ----
    {
        static const wchar_t A[] = L"abcdef", B[] = L"abcXef";
        for (int ca = 0; ca <= 6; ++ca)
            for (int cb = 0; cb <= 6; ++cb)
                CHECK(both(A, ca, B, cb), "every explicit count pair 0..6");
        CHECK(both(A, 3, B, 3), "equal within the bound");
        CHECK(both(A, -1, A, 6), "-1 and an explicit count agree");
    }

    // ---- EMBEDDED NULs: counted, so the comparison must not stop ----
    {
        static const wchar_t A[] = { L'a', 0, L'c', 0 };
        static const wchar_t B[] = { L'a', 0, L'd', 0 };
        static const wchar_t C[] = { L'a', 0, L'c', 0 };
        CHECK(both(A, 3, B, 3), "embedded NUL: differs after it");
        CHECK(both(A, 3, C, 3), "embedded NUL: equal across it");
        CHECK(both(A, -1, B, -1), "with -1 the same buffers stop at the NUL");
        CHECK(both(A, 2, B, 2), "bounded before the difference");
    }

    // ---- every code unit against its own upcase partner, both modes ----
    {
        for (unsigned c = 1; c < 0xFFFF; ++c) {
            wchar_t a[1], b[1];
            a[0] = (wchar_t)c; b[0] = (wchar_t)wia_upcase[c];
            if (!both(a, 1, b, 1)) { CHECK(0, "code unit vs its upcase partner"); if (fails > 8) break; }
        }
    }

    // ---- the ASCII/non-ASCII boundary: one non-ASCII character at every offset in a chunk ----
    {
        static const wchar_t NON[] = { 0x00E9, 0x00C9, 0x0430, 0x0410, 0x03C3, 0x03A3,
                                       0x0130, 0x0131, 0x1E9E, 0x00DF, 0xFF41, 0x8000, 0xFFFF };
        wchar_t a[80], b[80];
        for (int len = 1; len <= 40; ++len) {
            for (int pos = 0; pos < len; ++pos) {
                for (int k = 0; k < 13; ++k) {
                    for (int i = 0; i < len; ++i) { a[i] = (wchar_t)(L'a' + (i % 26));
                                                    b[i] = a[i]; }
                    a[len] = 0; b[len] = 0;
                    a[pos] = NON[k];
                    CHECK(both(a, len, b, len), "non-ASCII at every offset (guard placement)");
                    b[pos] = (wchar_t)wia_upcase[NON[k]];
                    CHECK(both(a, len, b, len), "non-ASCII vs its upcase at every offset");
                }
            }
        }
    }

    // ---- fuzz over content, length and mode ----
    {
        wchar_t a[300], b[300];
        for (int t = 0; t < 300000; ++t) {
            int la = (int)(rnd() % 80), lb;
            for (int i = 0; i < la; ++i) {
                unsigned k = rnd() % 10;
                a[i] = (k < 7) ? (wchar_t)(L'A' + rnd() % 58) : (wchar_t)(1 + rnd() % 0xFFFE);
            }
            a[la] = 0;
            lb = la;
            for (int i = 0; i <= la; ++i) b[i] = a[i];
            unsigned k = rnd() % 4;
            if (k == 0 && la) b[rnd() % la] = (wchar_t)(1 + rnd() % 0xFFFE);
            if (k == 1 && la) lb = (int)(rnd() % (la + 1));
            int ca = (rnd() % 4) ? la : -1;
            int cb = (rnd() % 4) ? lb : -1;
            CHECK(both(a, ca, b, cb), "fuzz");
        }
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (CompareStringOrdinal vs live kernelbase + oracle, BOTH modes on every "
           "case: NULL with its last-error, hand-picked orderings including \"a\" vs \"B\" where "
           "ignore-case orders by the UPCASED values and the raw code units would disagree, every "
           "explicit count pair 0..6 (a count is EXACT, only -1 means NUL-terminated), EMBEDDED NULs "
           "compared as ordinary characters, EVERY code unit against its own upcase partner, one "
           "non-ASCII character at EVERY offset of every length 1..40 over 13 awkward code points "
           "(which is what exercises the ASCII-guard placement in the vector fold and its 64K-table "
           "fallback), and 300k fuzz over content, length and mode)\n");
    return 0;
}
