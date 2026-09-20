/* discovery/lstrcmp_is_linguistic.c
   Is kernelbase!lstrcmpA / lstrcmpiA worth attacking, or is it another StrCmpNW?

   The narrow survey timed both at 0.90 GB/s -- 4435 ns to compare 4000 equal characters, the slowest
   thing it found. That looks like an enormous opportunity. But TWO earlier targets in this project
   were abandoned on exactly this kind of evidence, and one signal here already smells:

       lstrcmpA  4000 (equal)   4435.84 ns
       lstrcmpiA 4000 (equal)   4431.29 ns

   Those are the SAME number. A genuine ordinal compare gets case-insensitivity by folding, which is
   real work and costs something; a routine where ignoring case is free is one that was going to
   normalise every character anyway -- i.e. a LINGUISTIC comparison through the collation tables. If
   that is what this is, no amount of AVX2 reproduces it and the target is dead, exactly as:

     * StrCmpNW, orders linguistically; its sign disagreed with wcsncmp on 52 130 of 200 000 pairs;
     * StrChrIW; its case fold has 3236-member equivalence classes, because ignorable code points
                    collate as nothing.

   THREE TESTS, each of which independently kills the target:

     1. ORDER. Ordinal comparison is arithmetic on code units, so 'A' (0x41) < 'a' (0x61) and
        lstrcmp("A","a") MUST be negative. Windows' default collation sorts lowercase first, so a
        linguistic comparison returns POSITIVE. One call settles it.
     2. AGREEMENT WITH strcmp's SIGN, over a large random corpus. StrCmpNW died here.
     3. EQUIVALENCE-CLASS SIZE for the case-insensitive form. A table fold gives classes of 1-4
        members. An ignorable set gives thousands. StrChrIW died here.

   Plus the thing that matters if it IS linguistic: whether the answer depends on the thread locale,
   which a reimplementation would have to reproduce and cannot.                                    */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FNA)(const char*, const char*);
typedef int (WINAPI *FNW)(const wchar_t*, const wchar_t*);

static unsigned long sd = 0xC0FFEEu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }
static int sgn(int v){ return v < 0 ? -1 : (v > 0 ? 1 : 0); }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    if (!hk) hk = LoadLibraryW(L"kernel32.dll");

    FNA cmpA  = (FNA)GetProcAddress(hk, "lstrcmpA");
    FNA cmpiA = (FNA)GetProcAddress(hk, "lstrcmpiA");
    FNW cmpW  = (FNW)GetProcAddress(hk, "lstrcmpW");
    FNW cmpiW = (FNW)GetProcAddress(hk, "lstrcmpiW");
    if (!cmpA)  { hk = LoadLibraryW(L"kernel32.dll");
                  cmpA  = (FNA)GetProcAddress(hk, "lstrcmpA");
                  cmpiA = (FNA)GetProcAddress(hk, "lstrcmpiA");
                  cmpW  = (FNW)GetProcAddress(hk, "lstrcmpW");
                  cmpiW = (FNW)GetProcAddress(hk, "lstrcmpiW"); }
    if (!cmpA || !cmpiA || !cmpW || !cmpiW) { printf("cannot resolve the lstrcmp family\n"); return 1; }

    printf("=== TEST 1: does it order ORDINALLY or LINGUISTICALLY? ===\n");
    printf("Ordinal is arithmetic on code units: 'A' is 0x41, 'a' is 0x61, so \"A\" < \"a\" and the\n");
    printf("result MUST be negative. Windows' default collation sorts lowercase first and returns\n");
    printf("positive. One call decides whether this routine is reproducible at all.\n\n");
    {
        int r  = cmpA("A", "a");
        int rw = cmpW(L"A", L"a");
        printf("  lstrcmpA(\"A\",\"a\") = %+d   -> %s\n", r,
               r < 0 ? "ORDINAL (reproducible)" : "LINGUISTIC (collation tables -- NOT reproducible)");
        printf("  lstrcmpW(\"A\",\"a\") = %+d   -> %s\n", rw,
               rw < 0 ? "ORDINAL (reproducible)" : "LINGUISTIC (collation tables -- NOT reproducible)");
        printf("  strcmp  (\"A\",\"a\") = %+d   (the ordinal answer, for reference)\n\n",
               sgn(strcmp("A","a")));

        /* the classic collation giveaways: punctuation that sorts as nothing, and digits vs letters */
        printf("  a few more orderings, ordinal answer in brackets:\n");
        struct { const char* x; const char* y; } P[] = {
            {"a","B"}, {"co-op","coop"}, {"a-b","ab"}, {"_x","ax"}, {"1","a"}, {"\xE9""a","ea"},
        };
        for (int i = 0; i < 6; ++i) {
            int lr = sgn(cmpA(P[i].x, P[i].y));
            int orr = sgn(strcmp(P[i].x, P[i].y));
            printf("    %-8s vs %-8s  lstrcmpA %+d   [strcmp %+d]%s\n",
                   P[i].x, P[i].y, lr, orr, lr != orr ? "   <- DISAGREES" : "");
        }
    }

    printf("\n=== TEST 2: how often does the SIGN disagree with strcmp, over 200000 pairs? ===\n");
    printf("This is the test StrCmpNW died on: 52130 disagreements out of 200000.\n");
    {
        static char a[64], b[64];
        long diff = 0, total = 0;
        for (int t = 0; t < 200000; ++t) {
            int la = 1 + (int)(rnd() % 12), lb = 1 + (int)(rnd() % 12);
            for (int i = 0; i < la; ++i) a[i] = (char)(0x20 + rnd() % 0x5F);
            for (int i = 0; i < lb; ++i) b[i] = (char)(0x20 + rnd() % 0x5F);
            a[la] = 0; b[lb] = 0;
            if (sgn(cmpA(a,b)) != sgn(strcmp(a,b))) ++diff;
            ++total;
        }
        printf("  %ld of %ld pairs disagree with strcmp's sign (%.2f%%)\n",
               diff, total, 100.0*diff/total);
        printf("  => %s\n", diff == 0 ? "ORDINAL: a code-unit compare reproduces it"
                                      : "LINGUISTIC: no code-unit compare can reproduce this");
    }

    printf("\n=== TEST 3: how big are lstrcmpiA's equivalence classes? ===\n");
    printf("A table fold gives classes of 1-4. An ignorable set gives thousands -- StrChrIW's were\n");
    printf("3236 members, because code points that collate as nothing all compare equal.\n");
    {
        int cls[256]; int csize[256];
        for (int i = 0; i < 256; ++i) cls[i] = -1;
        int nclasses = 0, biggest = 0, biggest_rep = 0;
        for (int i = 1; i < 256; ++i) {
            if (cls[i] >= 0) continue;
            char s1[2]; s1[0] = (char)i; s1[1] = 0;
            int id = nclasses++;
            cls[i] = id; csize[id] = 1;
            for (int j = i+1; j < 256; ++j) {
                if (cls[j] >= 0) continue;
                char s2[2]; s2[0] = (char)j; s2[1] = 0;
                if (cmpiA(s1, s2) == 0) { cls[j] = id; ++csize[id]; }
            }
            if (csize[id] > biggest) { biggest = csize[id]; biggest_rep = i; }
        }
        printf("  over the 255 non-NUL byte values: %d classes, largest has %d members",
               nclasses, biggest);
        if (biggest > 1) {
            printf(" (representative 0x%02X:", biggest_rep);
            int shown = 0;
            for (int j = 1; j < 256 && shown < 12; ++j)
                if (cls[j] == cls[biggest_rep]) { printf(" %02X", j); ++shown; }
            printf(biggest > 12 ? " ...)" : ")");
        }
        printf("\n  => %s\n", biggest <= 4 ? "a real case fold, not an ignorable set"
                                           : "AN IGNORABLE SET: unreproducible without the collation data");
    }

    printf("\n=== TEST 4: if it is linguistic, does the answer follow the thread LOCALE? ===\n");
    printf("A reimplementation would have to reproduce that too, and cannot.\n");
    {
        static const wchar_t* LOC[] = { L"en-US", L"tr-TR", L"lt-LT", L"cs-CZ", L"da-DK" };
        struct { const char* x; const char* y; } P[] = {
            {"i","I"}, {"a","A"}, {"ch","d"}, {"aa","ab"}, {"o","\xF8"},
        };
        printf("  %-8s", "locale");
        for (int j = 0; j < 5; ++j) printf("  %s/%s", P[j].x, P[j].y);
        printf("\n");
        for (int k = 0; k < 5; ++k) {
            LCID l = LocaleNameToLCID(LOC[k], 0);
            if (!l) continue;
            SetThreadLocale(l);
            printf("  %-8ls", LOC[k]);
            for (int j = 0; j < 5; ++j) printf("  %+5d", sgn(cmpiA(P[j].x, P[j].y)));
            printf("\n");
        }
        SetThreadLocale(LocaleNameToLCID(L"en-US", 0));
    }

    printf("\n=== TEST 5: is lstrcmpiA really free, or does it just share the same machinery? ===\n");
    {
        LARGE_INTEGER F, qa, qb; QueryPerformanceFrequency(&F);
        SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
        static char big1[4096], big2[4096];
        for (int i = 0; i < 4000; ++i) { big1[i] = (char)('a' + i%26); big2[i] = big1[i]; }
        big1[4000] = big2[4000] = 0;
        volatile int sink = 0;
        double best_cs = 1e300, best_ci = 1e300, best_ord = 1e300;
        for (int t = 0; t < 5; ++t) {
            QueryPerformanceCounter(&qa);
            for (int i = 0; i < 2000; ++i) sink ^= cmpA(big1, big2);
            QueryPerformanceCounter(&qb);
            double ns = (double)(qb.QuadPart-qa.QuadPart)*1e9/F.QuadPart/2000;
            if (ns < best_cs) best_cs = ns;

            QueryPerformanceCounter(&qa);
            for (int i = 0; i < 2000; ++i) sink ^= cmpiA(big1, big2);
            QueryPerformanceCounter(&qb);
            ns = (double)(qb.QuadPart-qa.QuadPart)*1e9/F.QuadPart/2000;
            if (ns < best_ci) best_ci = ns;

            QueryPerformanceCounter(&qa);
            for (int i = 0; i < 2000; ++i) sink ^= strcmp(big1, big2);
            QueryPerformanceCounter(&qb);
            ns = (double)(qb.QuadPart-qa.QuadPart)*1e9/F.QuadPart/2000;
            if (ns < best_ord) best_ord = ns;
        }
        printf("  lstrcmpA  4000 equal: %8.2f ns\n", best_cs);
        printf("  lstrcmpiA 4000 equal: %8.2f ns   (%.2fx the case-sensitive cost)\n",
               best_ci, best_ci/best_cs);
        printf("  CRT strcmp 4000 equal:%8.2f ns   (what an ordinal compare costs)\n", best_ord);
        printf("  sink=%d\n", sink);
    }
    return 0;
}
