/* changes/251-pathissamerootw/correctness.c
 *
 * THIS CHANGE HAS TWO INDEPENDENTLY-CHECKABLE HALVES, and they are checked separately so a failure
 * says which one is wrong:
 *
 *   PART A -- the ROOT PARSER, three-way: our assembly, a C transcription of the same disassembly,
 *             and the live kernelbase!PathCchSkipRoot. Plus our wia_pathskiprootw against the live
 *             shlwapi!PathSkipRootW, which is the same thing with the "a bare C: is not a root"
 *             rule on top.
 *   PART B -- PathIsSameRootW itself, three-way: ours, an oracle that uses the LIVE root skip and
 *             the LIVE PathCommonPrefixW (so it isolates the three lines of arithmetic), and the
 *             live shlwapi!PathIsSameRootW.
 *
 * The observable in part B is a single BOOL, so its corpus is weighted towards paths that SHARE a
 * root: an implementation returning TRUE unconditionally must fail, and so must one returning FALSE.
 * Both counts are printed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int      wia_pathissamerootw(const wchar_t*, const wchar_t*);
extern wchar_t* wia_pathskiprootw(const wchar_t*);
extern int      wia_pathcchskiproot_len(const wchar_t*);
extern int      ref_pathissamerootw(const wchar_t*, const wchar_t*);
extern int      ref_skiproot_len(const wchar_t*);
extern int      ref_init(void);
extern void     wia_upcase_init(void);

typedef HRESULT  (WINAPI *FSKIP)(const wchar_t*, const wchar_t**);
typedef wchar_t* (WINAPI *FSKW)(const wchar_t*);
typedef BOOL     (WINAPI *FSAME)(const wchar_t*, const wchar_t*);
static FSKIP  sysskip;
static FSKW   sysskw;
static FSAME  syssame;

static long a_cases = 0, a_fail = 0, b_cases = 0, b_fail = 0, b_true = 0;
static int a_shown = 0, b_shown = 0;

static void shows(const wchar_t* s)
{
    int i;
    if (!s) { printf("(NULL)"); return; }
    printf("\"");
    for (i = 0; s[i] && i < 52; ++i)
        printf((s[i] >= 32 && s[i] < 127) ? "%c" : "\\u%04X", (unsigned)s[i]);
    printf("\"");
}

/* PART A: the root parser */
static void checkA(const wchar_t* s)
{
    const wchar_t* e = 0;
    HRESULT hr;
    int live, ours, orc;
    wchar_t* lskw;
    wchar_t* oskw;

    ++a_cases;
    hr = sysskip(s, &e);
    live = (hr < 0) ? -1 : (int)(e - s);
    ours = wia_pathcchskiproot_len(s);
    orc  = ref_skiproot_len(s);
    lskw = sysskw(s);
    oskw = wia_pathskiprootw(s);

    if (ours != live || orc != live || lskw != oskw) {
        ++a_fail;
        if (++a_shown <= 20) {
            printf("  A MISMATCH ");
            shows(s);
            printf("  live=%d ours=%d oracle=%d   SkipRootW live=%s ours=%s\n",
                   live, ours, orc, lskw ? "p+n" : "NULL", oskw ? "p+n" : "NULL");
            if (lskw && oskw && lskw != oskw)
                printf("      (and the two pointers differ: live p+%d, ours p+%d)\n",
                       (int)(lskw - s), (int)(oskw - s));
        }
    }
}

/* PART B: PathIsSameRootW */
static void checkB(const wchar_t* a, const wchar_t* b)
{
    int x, y, z;
    ++b_cases;
    x = wia_pathissamerootw(a, b) != 0;
    y = ref_pathissamerootw(a, b) != 0;
    z = syssame(a, b) != 0;
    if (x) ++b_true;
    if (x != z || x != y) {
        ++b_fail;
        if (++b_shown <= 20) {
            printf("  B MISMATCH ");
            shows(a); printf(" vs "); shows(b);
            printf("  ours=%d live=%d oracle=%d\n", x, z, y);
        }
    }
}

static uint64_t rs = 0x2545F4914F6CDD1Dull;
static unsigned rng(void){ rs = rs*6364136223846793005ull + 1442695040888963407ull;
                           return (unsigned)(rs >> 33); }

static const wchar_t* ROOTS[] = {
    L"C:\\", L"c:\\", L"D:\\", L"C:", L"\\\\srv\\share\\", L"\\\\srv\\share",
    L"\\\\SRV\\SHARE\\", L"\\\\srv\\other\\", L"\\\\?\\C:\\", L"\\\\?\\UNC\\srv\\share\\",
    L"\\", L"\\\\", L"\\\\\\", L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}\\",
    L"", L"rel", L"\\\\.\\C:\\",
};
#define NROOTS ((int)(sizeof ROOTS / sizeof ROOTS[0]))

int main(void)
{
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sysskip = (FSKIP)GetProcAddress(hk, "PathCchSkipRoot");
    sysskw  = (FSKW) GetProcAddress(hs, "PathSkipRootW");
    syssame = (FSAME)GetProcAddress(hs, "PathIsSameRootW");
    if (!sysskip || !sysskw || !syssame) { printf("resolve failed\n"); return 1; }
    if (!ref_init()) { printf("oracle resolve failed\n"); return 1; }
    wia_upcase_init();

    printf("PathIsSameRootW: the root parser, then the function\n\n");

    /* ================= PART A ================= */
    printf("PART A -- the root parser (ours vs a C transcription vs live PathCchSkipRoot,\n"
           "          and wia_pathskiprootw vs live PathSkipRootW)\n");
    {
        static const wchar_t A[] = L"\\a:?.";
        static wchar_t s[12];
        int len;
        long mark = a_cases;
        for (len = 0; len <= 7; ++len) {
            long tot = 1, v; int i;
            for (i = 0; i < len; ++i) tot *= 5;
            for (v = 0; v < tot; ++v) {
                long t = v;
                for (i = 0; i < len; ++i) { s[i] = A[t % 5]; t /= 5; }
                s[len] = 0;
                checkA(s);
            }
        }
        printf("  exhaustive over \"\\a:?.\" to length 7: %ld cases, %ld mismatches\n",
               a_cases - mark, a_fail);
    }
    {
        static const wchar_t A[] = L"\\UNCuV:a";
        static wchar_t s[24];
        int len;
        long mark = a_cases, f0 = a_fail;
        for (len = 0; len <= 5; ++len) {
            long tot = 1, v; int i;
            for (i = 0; i < len; ++i) tot *= 8;
            for (v = 0; v < tot; ++v) {
                long t = v;
                s[0] = L'\\'; s[1] = L'\\'; s[2] = L'?'; s[3] = L'\\';
                for (i = 0; i < len; ++i) { s[4 + i] = A[t % 8]; t /= 8; }
                s[4 + len] = 0;
                checkA(s);
                s[2] = L'.'; checkA(s);
                s[2] = L'x'; checkA(s);
            }
        }
        printf("  behind \"\\\\?\\\", \"\\\\.\\\" and \"\\\\x\\\": %ld cases, %ld mismatches\n",
               a_cases - mark, a_fail - f0);
    }
    {
        static const wchar_t G[] = L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}";
        static wchar_t s[80];
        int i, k;
        long mark = a_cases, f0 = a_fail;
        wcscpy(s, G); checkA(s);
        wcscat(s, L"\\"); checkA(s);
        wcscat(s, L"x"); checkA(s);
        for (i = 4; G[i]; ++i) {
            static const wchar_t M[] = L"\\-}{0gZ[";       /* '[' is the case-fold trap */
            for (k = 0; k < 8; ++k) {
                wcscpy(s, G); s[i] = M[k]; checkA(s);
                wcscat(s, L"\\"); checkA(s);
            }
        }
        for (i = 5; i < 48; ++i) { wcsncpy(s, G, i); s[i] = 0; checkA(s);
                                   s[i] = L'\\'; s[i + 1] = 0; checkA(s); }
        printf("  the volume form, perturbed and truncated: %ld cases, %ld mismatches\n",
               a_cases - mark, a_fail - f0);
    }
    {
        long mark = a_cases, f0 = a_fail;
        static const wchar_t* T[] = {
            L"C:\\Windows\\System32", L"\\\\srv\\share\\dir\\file", L"\\\\?\\C:\\a\\b",
            L"\\\\?\\UNC\\srv\\share\\dir", L"\\\\?\\unc\\srv\\share\\", L"\\\\.\\PhysicalDrive0",
            L"relative\\path", L"c:", L"Z:\\", L"\\\\?\\GLOBALROOT\\Device\\X",
        };
        int i;
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) checkA(T[i]);
        checkA(L"");
        printf("  realistic paths: %ld cases, %ld mismatches\n\n", a_cases - mark, a_fail - f0);
    }

    /* ================= PART B ================= */
    printf("PART B -- PathIsSameRootW (ours vs an envelope-only oracle vs the live export)\n");
    {
        long mark = b_cases, f0 = b_fail, t0 = b_true;
        int i, j;
        for (i = 0; i < NROOTS; ++i)
            for (j = 0; j < NROOTS; ++j)
                checkB(ROOTS[i], ROOTS[j]);
        printf("  every root against every root (%d x %d): %ld cases, %ld mismatches, %ld TRUE\n",
               NROOTS, NROOTS, b_cases - mark, b_fail - f0, b_true - t0);
    }
    {
        /* roots extended with real tails, so the answer is decided by the ROOT and not the length */
        long mark = b_cases, f0 = b_fail, t0 = b_true;
        static const wchar_t* TAIL[] = { L"", L"a", L"a\\b", L"a\\b\\c", L"\\", L"dir\\file.txt" };
        static wchar_t x[200], y[200];
        int i, j, ti, tj;
        for (i = 0; i < NROOTS; ++i)
            for (j = 0; j < NROOTS; ++j)
                for (ti = 0; ti < 6; ++ti)
                    for (tj = 0; tj < 6; ++tj) {
                        wcscpy(x, ROOTS[i]); wcscat(x, TAIL[ti]);
                        wcscpy(y, ROOTS[j]); wcscat(y, TAIL[tj]);
                        checkB(x, y);
                    }
        printf("  roots x tails: %ld cases, %ld mismatches, %ld TRUE\n",
               b_cases - mark, b_fail - f0, b_true - t0);
    }
    {
        long mark = b_cases, f0 = b_fail, t0 = b_true;
        static wchar_t x[400], y[400];
        int n, k;
        printf("  long paths under a shared root, at every length 1..300\n");
        for (n = 1; n <= 300; ++n) {
            wcscpy(x, L"C:\\");
            for (k = 0; k < n; ++k) x[3 + k] = (k % 7 == 6) ? L'\\' : (wchar_t)(L'a' + k % 26);
            x[3 + n] = 0;
            wcscpy(y, x);
            checkB(x, y);
            y[3] = L'#';                       /* same root, divergent immediately */
            checkB(x, y);
            wcscpy(y, L"D:\\"); wcscat(y, x + 3);
            checkB(x, y);                      /* a DIFFERENT root, same tail */
        }
        printf("  %ld cases, %ld mismatches, %ld TRUE\n", b_cases - mark, b_fail - f0, b_true - t0);
    }
    {
        long mark = b_cases, f0 = b_fail, t0 = b_true;
        static wchar_t x[120], y[120];
        int t;
        printf("  fuzz: 200000 pairs built from a root plus a random tail\n");
        for (t = 0; t < 200000; ++t) {
            static const wchar_t A[] = L"ab\\:.";
            int la = (int)(rng() % 14), lb = (int)(rng() % 14), k;
            const wchar_t* ra = ROOTS[rng() % NROOTS];
            const wchar_t* rb = ROOTS[rng() % NROOTS];
            wcscpy(x, ra); wcscpy(y, rb);
            { int n = (int)wcslen(x); for (k = 0; k < la; ++k) x[n + k] = A[rng() % 5];
              x[n + la] = 0; }
            { int n = (int)wcslen(y); for (k = 0; k < lb; ++k) y[n + k] = A[rng() % 5];
              y[n + lb] = 0; }
            checkB(x, y);
        }
        printf("  %ld cases, %ld mismatches, %ld TRUE\n", b_cases - mark, b_fail - f0, b_true - t0);
    }
    {
        printf("  NULL arguments\n");
        checkB(0, L"C:\\a");
        checkB(L"C:\\a", 0);
        checkB(0, 0);
        printf("  3 cases, %ld mismatches so far\n", b_fail);
    }
    {   /* a path ending at a guard page: the root parser reads ahead by up to 8 characters */
        SYSTEM_INFO si; GetSystemInfo(&si);
        {
            SIZE_T pg = si.dwPageSize;
            char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (g) {
                DWORD old;
                int tail, bad = 0;
                VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
                for (tail = 2; tail <= 60; ++tail) {
                    wchar_t* s = (wchar_t*)((g + pg) - tail * 2);
                    int k, fa = 0, fc = 0, ra = 0, rc = 0;
                    for (k = 0; k < tail - 1; ++k)
                        s[k] = (k == 1) ? L':' : ((k % 5 == 4) ? L'\\' : (wchar_t)(L'a' + k % 26));
                    s[tail - 1] = 0;
                    __try { ra = wia_pathissamerootw(s, s) != 0; }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
                    __try { rc = syssame(s, s) != 0; }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
                    ++b_cases;
                    if (fa || fc || ra != rc) { ++b_fail; ++bad; }
                }
                printf("  a path ending at a PAGE_NOACCESS boundary, 59 lengths: %s\n",
                       bad ? "MISMATCHES" : "never faults, identical to the live export");
                VirtualFree(g, 0, MEM_RELEASE);
            }
        }
    }

    printf("\nPART A: %ld cases, %ld mismatches\nPART B: %ld cases, %ld mismatches (%ld TRUE)\n",
           a_cases, a_fail, b_cases, b_fail, b_true);
    printf("%ld cases total, %ld mismatches -- %s\n", a_cases + b_cases, a_fail + b_fail,
           (a_fail + b_fail) ? "CORRECTNESS FAILED" : "bit-exact");
    return (a_fail + b_fail) ? 1 : 0;
}
