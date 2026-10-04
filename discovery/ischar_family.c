/* discovery/ischar_family.c
   user32's character classifiers: IsCharAlphaW, IsCharAlphaNumericW, IsCharUpperW, IsCharLowerW.

   WHY. Change 302 found user32 spending ~12 ns on CharUpperW's character mode, which is one table read
   once the mode test is done, and was beaten 3.7x-4.8x by doing exactly that. These four classify one
   character each and are called a character at a time by whatever is scanning text. Change 287
   established that the CT_CTYPE1 table behind GetStringTypeW is context-free and invariant across
   seven locales; if these are bit tests on that table, each is one load.

   ESTABLISHED HERE, nothing assumed:
     1. the exact formula for each, as a bit test on GetStringTypeW(CT_CTYPE1), over all 65536 units,
        with every disagreement counted rather than the first one reported;
     2. the exact RETURN VALUE -- TRUE, or the raw class bit -- because a caller comparing to TRUE
        would see the difference;
     3. invariance under SetThreadLocale and SetThreadUILanguage to tr-TR, ja-JP, ar-SA, ru-RU;
     4. ns per call against GetStringTypeW for one character.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef BOOL (WINAPI *PFN)(WCHAR);
static PFN F[4];
static const char* NM[4] = { "IsCharAlphaW", "IsCharAlphaNumericW", "IsCharUpperW", "IsCharLowerW" };
static volatile uint64_t sink;

static WORD ct1(WCHAR c) { WORD t = 0; GetStringTypeW(CT_CTYPE1, &c, 1, &t); return t; }

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"user32.dll");
    for (int i = 0; i < 4; ++i) F[i] = (PFN)GetProcAddress(u, NM[i]);

    static WORD T[65536];
    for (int c = 0; c < 65536; ++c) T[c] = ct1((WCHAR)c);

    printf("== 1. formulas over all 65536 code units (disagreements) ==\n");
    struct { const char* f; WORD mask; } cand[] = {
        { "C1_ALPHA", C1_ALPHA }, { "C1_ALPHA|C1_DIGIT", C1_ALPHA | C1_DIGIT },
        { "C1_UPPER", C1_UPPER }, { "C1_LOWER", C1_LOWER },
    };
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 4; ++k) {
            int bad = 0, firstbad = -1;
            for (int c = 0; c < 65536; ++c) {
                int want = (T[c] & cand[k].mask) != 0;
                int got  = F[i]((WCHAR)c) != 0;
                if (want != got) { if (firstbad < 0) firstbad = c; ++bad; }
            }
            if (bad < 2000) printf("  %-20s vs %-18s: %6d disagree%s", NM[i], cand[k].f, bad, bad ? "" : "   <== exact");
            if (bad && bad < 2000) printf("  (first U+%04X: ctype1=%04X)", firstbad, T[firstbad]);
            if (bad < 2000) printf("\n");
        }
    }

    printf("\n== 2. raw return values seen ==\n");
    for (int i = 0; i < 4; ++i) {
        unsigned seen[8]; int ns = 0;
        for (int c = 0; c < 65536 && ns < 8; ++c) {
            unsigned r = (unsigned)F[i]((WCHAR)c);
            int dup = 0; for (int j = 0; j < ns; ++j) if (seen[j] == r) dup = 1;
            if (!dup) seen[ns++] = r;
        }
        printf("  %-20s:", NM[i]); for (int j = 0; j < ns; ++j) printf(" 0x%X", seen[j]); printf("\n");
    }

    printf("\n== 3. locale invariance ==\n");
    {
        static const LCID L[] = { 0x041F, 0x0411, 0x0401, 0x0419 };   /* tr-TR ja-JP ar-SA ru-RU */
        static const char* LN[] = { "tr-TR", "ja-JP", "ar-SA", "ru-RU" };
        static BOOL base[4][65536];
        for (int i = 0; i < 4; ++i) for (int c = 0; c < 65536; ++c) base[i][c] = F[i]((WCHAR)c);
        for (int l = 0; l < 4; ++l) {
            SetThreadLocale(L[l]); SetThreadUILanguage(LANGIDFROMLCID(L[l]));
            int diff = 0;
            for (int i = 0; i < 4; ++i) for (int c = 0; c < 65536; ++c) if (F[i]((WCHAR)c) != base[i][c]) ++diff;
            printf("  thread locale %s: %d differences across the four\n", LN[l], diff);
        }
        SetThreadLocale(LOCALE_USER_DEFAULT);
    }

    printf("\n== 4. ns per call ==\n");
    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    const int N = 2000000;
    for (int i = 0; i < 4; ++i) {
        double best = 1e300;
        for (int t = 0; t < 5; ++t) {
            LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
            for (int k = 0; k < N; ++k) sink += F[i]((WCHAR)(L'a' + (k & 15)));
            QueryPerformanceCounter(&b);
            double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / f.QuadPart / N; if (ns < best) best = ns;
        }
        printf("  %-20s %.2f ns per call\n", NM[i], best);
    }
    {
        double best = 1e300;
        for (int t = 0; t < 5; ++t) {
            LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
            for (int k = 0; k < N; ++k) sink += ct1((WCHAR)(L'a' + (k & 15)));
            QueryPerformanceCounter(&b);
            double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / f.QuadPart / N; if (ns < best) best = ns;
        }
        printf("  %-20s %.2f ns per call (one character)\n", "GetStringTypeW", best);
    }
    return 0;
}
