/* changes/254-findstringordinal/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE kernelbase!FindStringOrdinal.
 *
 * BOTH OBSERVABLES ARE COMPARED, not just the return value. This is a Win32 API: it sets a
 * last-error, and a reimplementation that returned the right index while leaving the wrong error
 * behind would be wrong in a way no index comparison could see. Every case checks the returned
 * index AND GetLastError(), against both the oracle and the live export.
 *
 * THE CORPORA ARE SPLIT BY WHICH PATH THEY REACH, which is the lesson of change 248 (1 085 965
 * enumerated cases passed in silence while the vector path was broken, because a six-character
 * string never reaches it) and of change 252 (whose corpora were split the same way):
 *
 *   1. THE REFUSALS           -- every validation branch, and the exact last-error each sets
 *   2. EXHAUSTIVE, short      -- all four modes; reaches only the scalar tail
 *   3. THE BLOCK BOUNDARY     -- every n-m from 0 to 40, planted at EVERY offset, all four modes:
 *                                this is where the forward loop bound and the BACKWARD one live,
 *                                and the backward walk is the half with no precedent in 252
 *   4. RANDOMISED, long       -- reaches the vector loops, over four alphabets
 *   5. THE FOLD               -- real fold pairs, AND pairs the NT ordinal table refuses to merge
 *   6. A GUARD PAGE           -- a PAGE_NOACCESS page against the end of the source, so an
 *                                over-read faults rather than merely disagreeing
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define F_STARTSWITH 0x00100000
#define F_ENDSWITH   0x00200000
#define F_FROMSTART  0x00400000
#define F_FROMEND    0x00800000

int  wia_findstringordinal(DWORD, const wchar_t*, int, const wchar_t*, int, BOOL);
int  ref_findstringordinal(DWORD, const wchar_t*, int, const wchar_t*, int, BOOL);
void ref_init(void);
int  wia_casemate_init(void);

typedef int (WINAPI *FFSO)(DWORD, LPCWSTR, int, LPCWSTR, int, BOOL);
static FFSO live;

static long fails = 0, cases = 0;

static void one(DWORD fl, const wchar_t* s, int cs, const wchar_t* v, int cv, BOOL ic,
                const char* where)
{
    int ro, rr, rl;
    DWORD eo, er, el;
    ++cases;
    SetLastError(0xDEAD); ro = wia_findstringordinal(fl, s, cs, v, cv, ic); eo = GetLastError();
    SetLastError(0xDEAD); rr = ref_findstringordinal(fl, s, cs, v, cv, ic); er = GetLastError();
    SetLastError(0xDEAD); rl = live(fl, s, cs, v, cv, ic);                  el = GetLastError();
    if (ro != rr || ro != rl || eo != er || eo != el) {
        if (++fails <= 25)
            printf("  MISMATCH [%s] flags=%08lX cs=%d cv=%d ic=%d  ret ours=%d ref=%d live=%d  "
                   "err ours=%lu ref=%lu live=%lu\n",
                   where, fl, cs, cv, (int)ic, ro, rr, rl, eo, er, el);
    }
}

static unsigned long long rs = 0x5DEECE66Dull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static const DWORD MODES[4] = { F_FROMSTART, F_FROMEND, F_STARTSWITH, F_ENDSWITH };

int main(void)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    int mi;
    if (!k) k = LoadLibraryW(L"kernelbase.dll");
    live = (FFSO)GetProcAddress(k, "FindStringOrdinal");
    if (!live) { printf("FindStringOrdinal not found\n"); return 1; }
    ref_init();

    printf("== CORRECTNESS: FindStringOrdinal (ours vs oracle vs LIVE kernelbase) ==\n");
    printf("   every case compares the returned index AND GetLastError()\n");
    {
        int mc = wia_casemate_init();
        printf("  0. largest case-equivalence class in the OS upcase table: %d\n", mc);
        if (mc != 2) { printf("  FAIL: the vector filter needs at most TWO members\n"); ++fails; }
    }

    /* ---- 1. the refusals ---- */
    {
        static const wchar_t* H = L"abcXYZabcXYZ";
        long before = cases;
        DWORD f;
        int ic;
        static const DWORD BAD[] = {
            F_FROMSTART | F_FROMEND, F_STARTSWITH | F_ENDSWITH, F_FROMSTART | 1,
            F_FROMSTART | 0x01000000, 1, 0x10000000, F_FROMSTART | 0x10, 0xFFFFFFFF,
            F_FROMSTART | F_STARTSWITH, 0x00F00000,
        };
        int i;
        for (i = 0; i < (int)(sizeof BAD / sizeof BAD[0]); ++i)
            one(BAD[i], H, -1, L"abc", -1, FALSE, "bad flags");
        one(0, H, -1, L"abc", -1, FALSE, "flags=0 defaults to FROMSTART");
        for (ic = -3; ic <= 4; ++ic)
            one(F_FROMSTART, H, -1, L"abc", -1, (BOOL)ic, "bIgnoreCase range");
        for (i = 0; i < 4; ++i) {
            f = MODES[i];
            one(f, 0,   -1, L"abc", -1, FALSE, "src NULL");
            one(f, H,   -1, 0,      -1, FALSE, "val NULL");
            one(f, 0,    0, 0,       0, FALSE, "both NULL, both lengths 0");
            one(f, H,   -2, L"abc", -1, FALSE, "cchSrc < -1");
            one(f, H,   -1, L"abc", -2, FALSE, "cchVal < -1");
            one(f, H,   -5, L"abc", -7, FALSE, "both < -1");
            one(f, H,    0, L"abc", -1, FALSE, "empty haystack");
            one(f, H,   -1, L"abc",  0, FALSE, "empty needle");
            one(f, H,    0, L"abc",  0, FALSE, "both empty");
            one(f, L"ab", -1, L"abc", -1, FALSE, "needle longer");
        }
        printf("  1. refusals and defaults, all four modes: %ld cases\n", cases - before);
    }

    /* ---- 2. EXHAUSTIVE over a three-letter alphabet, all four modes ---- */
    {
        static const wchar_t A[3] = { L'a', L'A', L'b' };
        long before = cases;
        int hl, nl;
        for (hl = 0; hl <= 5; ++hl) {
            long hc = 1, hi;
            int k;
            for (k = 0; k < hl; ++k) hc *= 3;
            for (hi = 0; hi < hc; ++hi) {
                wchar_t h[8];
                long t = hi;
                for (k = 0; k < hl; ++k) { h[k] = A[t % 3]; t /= 3; }
                for (nl = 0; nl <= 3; ++nl) {
                    long nc = 1, ni;
                    for (k = 0; k < nl; ++k) nc *= 3;
                    for (ni = 0; ni < nc; ++ni) {
                        wchar_t nb[8];
                        long u = ni;
                        for (k = 0; k < nl; ++k) { nb[k] = A[u % 3]; u /= 3; }
                        for (mi = 0; mi < 4; ++mi) {
                            one(MODES[mi], h, hl, nb, nl, FALSE, "exhaustive");
                            one(MODES[mi], h, hl, nb, nl, TRUE,  "exhaustive ci");
                        }
                    }
                }
            }
        }
        printf("  2. exhaustive {a,A,b}, haystack 0..5 x needle 0..3, 4 modes x 2: %ld cases\n",
               cases - before);
    }

    /* ---- 3. the block boundary, planted at EVERY offset, all four modes ---- */
    {
        long before = cases;
        int span, m, pos;
        for (span = 0; span <= 40; ++span)
            for (m = 1; m <= 12; ++m) {
                int n = span + m, i;
                wchar_t h[128], nb[32];
                if (n > 120) continue;
                for (i = 0; i < n; ++i) h[i] = (wchar_t)(L'a' + (rnd() % 3));
                for (i = 0; i < m; ++i) nb[i] = (wchar_t)(L'a' + (rnd() % 3));
                for (mi = 0; mi < 4; ++mi) one(MODES[mi], h, n, nb, m, FALSE, "boundary/random");
                for (pos = 0; pos <= span; ++pos) {
                    for (i = 0; i < m; ++i) h[pos + i] = nb[i];
                    for (mi = 0; mi < 4; ++mi) {
                        one(MODES[mi], h, n, nb, m, FALSE, "boundary/planted");
                        one(MODES[mi], h, n, nb, m, TRUE,  "boundary/planted ci");
                    }
                    for (i = 0; i < m; ++i) h[pos + i] = (wchar_t)(L'a' + (rnd() % 3));
                }
                /* and TWO plants, so FROMSTART and FROMEND must disagree */
                if (span >= m + 2) {
                    for (i = 0; i < m; ++i) { h[i] = nb[i]; h[span - m + i] = nb[i]; }
                    for (mi = 0; mi < 4; ++mi) one(MODES[mi], h, n, nb, m, FALSE, "two plants");
                }
            }
        printf("  3. block boundary, n-m = 0..40 x m = 1..12, planted at every offset, 4 modes: "
               "%ld cases\n", cases - before);
    }

    /* ---- 4. randomised, long ---- */
    {
        long before = cases;
        int trial;
        static const wchar_t* ALPHA[4] = {
            L"ab",
            L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
            L"\x00e0\x00c0\x00e8\x00c8\x00ff\x0178\x03b1\x0391\x0430\x0410",
            L"abcXYZ\x00e0\x0410\x0430\xd800\xdc00"
        };
        for (trial = 0; trial < 30000; ++trial) {
            wchar_t h[512], nb[64];
            const wchar_t* al = ALPHA[rnd() % 4];
            int alen = (int)wcslen(al);
            int n = 1 + (int)(rnd() % 300);
            int m = 1 + (int)(rnd() % 16);
            int i, ic = (int)(rnd() & 1);
            DWORD f = MODES[rnd() % 4];
            if (m > n) m = n;
            for (i = 0; i < n; ++i) h[i] = al[rnd() % alen];
            for (i = 0; i < m; ++i) nb[i] = al[rnd() % alen];
            if (rnd() & 1) {
                int pos = (int)(rnd() % (unsigned)(n - m + 1));
                for (i = 0; i < m; ++i) {
                    wchar_t c = nb[i];
                    if ((rnd() & 1) && c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
                    h[pos + i] = c;
                }
            }
            if (rnd() & 1) { for (i = 0; i < m; ++i) h[n - m + i] = nb[i]; }  /* often ENDSWITH */
            one(f, h, n, nb, m, (BOOL)ic, "random/long");
            one(f, h, -1, nb, -1, (BOOL)ic, "random/long, cch = -1");
        }
        printf("  4. randomised long strings, 4 alphabets, counted AND -1 lengths: %ld cases\n",
               cases - before);
    }

    /* ---- 5. the fold, at pairs that DO merge and pairs that deliberately DO NOT ---- */
    {
        static const struct { wchar_t a, b; const char* what; } P[] = {
            { 0x00E0, 0x00C0, "a-grave / A-grave -- MERGE"      },
            { 0x00FF, 0x0178, "y-diaeresis / Y-diaeresis -- MERGE" },
            { 0x03B1, 0x0391, "greek alpha / ALPHA -- MERGE"    },
            { 0x0430, 0x0410, "cyrillic a / A -- MERGE"         },
            { 0xFF41, 0xFF21, "fullwidth a / A -- MERGE"        },
            { 0x017F, 0x0053, "LONG S / ASCII S -- do NOT merge"},
            { 0x0131, 0x0049, "dotless i / ASCII I -- do NOT"   },
            { 0x00DF, 0x1E9E, "sharp s / capital -- do NOT"     },
            { 0x00B5, 0x039C, "micro / MU -- do NOT"            },
        };
        long before = cases;
        int i, k;
        for (i = 0; i < (int)(sizeof P / sizeof P[0]); ++i)
            for (k = 0; k < 2; ++k) {
                int n = k ? 200 : 5, pos;
                for (pos = 0; pos + 3 <= n; pos += (k ? 23 : 1)) {
                    wchar_t h[256], nb[8];
                    int j;
                    for (j = 0; j < n; ++j) h[j] = L'q';
                    h[pos] = L'x'; h[pos + 1] = P[i].a; h[pos + 2] = L'y';
                    nb[0] = L'x'; nb[1] = P[i].b; nb[2] = L'y';
                    for (mi = 0; mi < 4; ++mi) {
                        one(MODES[mi], h, n, nb, 3, FALSE, P[i].what);
                        one(MODES[mi], h, n, nb, 3, TRUE,  P[i].what);
                    }
                    /* and as the ANCHORS, which is what the vector filter has to get exactly right */
                    nb[0] = P[i].b; nb[1] = L'y'; nb[2] = P[i].b;
                    h[pos] = P[i].a; h[pos + 1] = L'y'; h[pos + 2] = P[i].a;
                    one(F_FROMSTART, h, n, nb, 3, TRUE, P[i].what);
                    one(F_FROMEND,   h, n, nb, 3, TRUE, P[i].what);
                }
            }
        printf("  5. fold pairs that merge AND pairs the ordinal table refuses to merge: %ld cases\n",
               cases - before);
    }

    /* ---- 6. a guard page against the end of the SOURCE ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        int n, m;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  6. guard page SKIPPED\n");
        } else {
            for (n = 1; n <= 260; ++n) {
                wchar_t* h = (wchar_t*)(base + si.dwPageSize) - n;
                wchar_t nb[32];
                int i;
                for (i = 0; i < n; ++i) h[i] = (wchar_t)(L'a' + (i % 3));
                for (m = 1; m <= 12 && m <= n; ++m) {
                    for (i = 0; i < m; ++i) nb[i] = L'z';           /* absent: a FULL scan */
                    for (mi = 0; mi < 4; ++mi) { one(MODES[mi], h, n, nb, m, FALSE, "guard/miss");
                                                 ++guard; }
                    for (i = 0; i < m; ++i) nb[i] = h[n - m + i];   /* a hit at the very end */
                    for (mi = 0; mi < 4; ++mi) { one(MODES[mi], h, n, nb, m, TRUE, "guard/tail");
                                                 ++guard; }
                }
            }
            printf("  6. guard page against the end of the source, n=1..260 x m=1..12 x 4 modes: "
                   "%ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (index AND last-error exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
