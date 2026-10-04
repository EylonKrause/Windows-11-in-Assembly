/* changes/304-strcmpcw/correctness.c
 * Gate 1: wia_strcmp{c,ic,nc,nic}w against live shlwapi!StrCmpCW / StrCmpICW / StrCmpNCW / StrCmpNICW
 * and the scalar oracle, comparing the exact return value -- the difference, not just its sign.
 *
 *   1. one-unit strings: every unit 0..FFFF against a probe set, and every pair over 0..17F
 *   2. strings of 0..200 units at every byte offset 0..33 for a and a spread of offsets for b, odd ones
 *      included, equal, or unequal at every position, or equal under the fold only; N forms with n
 *      around every interesting index, and negative
 *   3. random strings over a tiny alphabet that is all case pairs and the characters around them
 *   4. both strings ending as the last wchar before a NOACCESS page, at every length and parity --
 *      the block loads must never read into the next page
 *   5. unterminated strings running into NOACCESS: equal up to the page -> both must fault;
 *      unequal before it -> neither may fault, and the values must agree
 *   6. N forms whose index n is the first unreadable unit: no fault
 *   7. NULL: n == 0 returns 0; otherwise both fault
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef int (WINAPI *PCMP)(PCWSTR, PCWSTR);
typedef int (WINAPI *PCMPN)(PCWSTR, PCWSTR, int);
extern int wia_strcmpcw(PCWSTR, PCWSTR);
extern int wia_strcmpicw(PCWSTR, PCWSTR);
extern int wia_strcmpncw(PCWSTR, PCWSTR, int);
extern int wia_strcmpnicw(PCWSTR, PCWSTR, int);
int ref_cmpw(const wchar_t*, const wchar_t*, int, int, int);

static PCMP sCW, sICW; static PCMPN sNCW, sNICW;
static long long tested, fails;
static const char* NM[4] = { "StrCmpCW", "StrCmpICW", "StrCmpNCW", "StrCmpNICW" };

static int call_sys(int f, const wchar_t* a, const wchar_t* b, int n) {
    switch (f) { case 0: return sCW(a, b); case 1: return sICW(a, b); case 2: return sNCW(a, b, n); default: return sNICW(a, b, n); }
}
static int call_ours(int f, const wchar_t* a, const wchar_t* b, int n) {
    switch (f) { case 0: return wia_strcmpcw(a, b); case 1: return wia_strcmpicw(a, b); case 2: return wia_strcmpncw(a, b, n); default: return wia_strcmpnicw(a, b, n); }
}

static void chk(int f, const wchar_t* a, const wchar_t* b, int n, const char* what) {
    int rs = call_sys(f, a, b, n), ro = call_ours(f, a, b, n), rr = ref_cmpw(a, b, f & 1, f >> 1, n);
    ++tested;
    if (rs != ro || rs != rr) {
        if (fails < 20) printf("FAIL %s %s n=%d: sys %d ours %d ref %d\n", NM[f], what, n, rs, ro, rr);
        ++fails;
    }
}
static void chk_all(const wchar_t* a, const wchar_t* b, int len, const char* what) {
    chk(0, a, b, 0, what); chk(1, a, b, 0, what);
    int ns[] = { 0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, len - 1, len, len + 1, len + 40, -1, -7, 0x7FFFFFFF, (int)0x80000000 };
    for (int i = 0; i < (int)(sizeof ns / sizeof ns[0]); ++i) { chk(2, a, b, ns[i], what); chk(3, a, b, ns[i], what); }
}

static unsigned rng = 12345u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static int faults(int f, int ours, const wchar_t* a, const wchar_t* b, int n, int* r) {
    __try { *r = ours ? call_ours(f, a, b, n) : call_sys(f, a, b, n); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sCW = (PCMP)GetProcAddress(h, "StrCmpCW");   sICW = (PCMP)GetProcAddress(h, "StrCmpICW");
    sNCW = (PCMPN)GetProcAddress(h, "StrCmpNCW"); sNICW = (PCMPN)GetProcAddress(h, "StrCmpNICW");
    if (!sCW || !sICW || !sNCW || !sNICW) { printf("missing export\n"); return 2; }

    /* 1 */
    {
        static const int P[] = { 0, 0x40, 0x41, 0x5A, 0x5B, 0x60, 0x61, 0x7A, 0x7B, 0x7F, 0x80, 0xC0, 0xE0, 0xFF, 0x100,
                                 0x130, 0x131, 0x7FFF, 0x8000, 0x8041, 0xD800, 0xDFFF, 0xFF21, 0xFF41, 0xFFFF };
        wchar_t a[2] = { 0 }, b[2] = { 0 };
        for (int x = 0; x <= 0xFFFF; ++x)
            for (int k = 0; k < (int)(sizeof P / sizeof P[0]); ++k) {
                a[0] = (wchar_t)x; b[0] = (wchar_t)P[k];
                for (int f = 0; f < 4; ++f) { chk(f, a, b, 1, "unit"); chk(f, b, a, 1, "unit"); }
            }
        for (int x = 0; x <= 0x17F; ++x)
            for (int y = 0; y <= 0x17F; ++y) {
                a[0] = (wchar_t)x; b[0] = (wchar_t)y;
                for (int f = 0; f < 4; ++f) chk(f, a, b, -1, "pair");
            }
        printf("  1. one-unit strings:                 %lld fails\n", fails);
    }

    /* 2 */
    {
        long long f0 = fails;
        static wchar_t A[1024], B[1024];
        static const int BO[] = { 0, 1, 2, 3, 6, 17, 30, 31, 33 };
        for (int len = 0; len <= 200; len += (len < 40 ? 1 : 7))
            for (int ao = 0; ao < 34; ao += (len < 40 ? 1 : 5))
                for (int bi = 0; bi < (int)(sizeof BO / sizeof BO[0]); ++bi) {
                    wchar_t* a = (wchar_t*)((char*)A + 64 + ao);
                    wchar_t* b = (wchar_t*)((char*)B + 64 + BO[bi]);
                    for (int i = 0; i < len; ++i) { unsigned r = rnd(); a[i] = (wchar_t)((r & 7) ? 'a' + r % 26 : 1 + (r >> 4) % 0xFFFE); }
                    a[len] = 0;
                    memcpy(b, a, (len + 1) * 2);
                    chk_all(a, b, len, "equal");
                    /* unequal at every position (and the terminator replaced) */
                    for (int p = 0; p <= len; ++p) {
                        wchar_t save = b[p];
                        unsigned r = rnd();
                        wchar_t v = (r & 1) ? (wchar_t)(save + 1 + (r >> 1) % 3) : (wchar_t)(save ? save - 1 : 0x41);
                        if (r % 5 == 0) v = 0;
                        if (v == save) v ^= 1;
                        b[p] = v;
                        chk_all(a, b, len, "unequal");
                        chk_all(b, a, len, "unequal-swapped");
                        b[p] = save;
                    }
                    /* equal only under the fold */
                    for (int i = 0; i < len; ++i) if (b[i] >= 'a' && b[i] <= 'z' && (rnd() & 1)) b[i] -= 32;
                    chk_all(a, b, len, "fold-equal");
                }
        printf("  2. strings x offsets x positions:    %lld fails\n", fails - f0);
    }

    /* 3 */
    {
        long long f0 = fails;
        static const wchar_t AL[] = { 'a', 'A', 'z', 'Z', '[', '`', '@', '{', 0xE0, 0xC0 };
        static wchar_t A[600], B[600];
        for (int t = 0; t < 60000; ++t) {
            int la = rnd() % 70, lb = rnd() % 70;
            int ao = rnd() % 34, bo = rnd() % 34;
            wchar_t* a = (wchar_t*)((char*)A + 64 + ao);
            wchar_t* b = (wchar_t*)((char*)B + 64 + bo);
            for (int i = 0; i < la; ++i) a[i] = AL[rnd() % 10];
            a[la] = 0;
            int share = rnd() % (la + 1);
            for (int i = 0; i < lb; ++i) b[i] = (i < share && i < la) ? ((rnd() % 3) ? a[i] : AL[rnd() % 10]) : AL[rnd() % 10];
            b[lb] = 0;
            for (int f = 0; f < 4; ++f) chk(f, a, b, (int)(rnd() % 80) - 3, "fuzz");
        }
        printf("  3. small-alphabet fuzz:              %lld fails\n", fails - f0);
    }

    /* 4 */
    {
        long long f0 = fails;
        unsigned char* pa = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        unsigned char* pb = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(pa, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(pb, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t far_[700];
        for (int odd = 0; odd < 4; ++odd)
            for (int len = 0; len <= 300; ++len) {
                wchar_t* a = (wchar_t*)(pa + 0x2000 - (len + 1) * 2 - (odd & 1));
                wchar_t* b = (wchar_t*)(pb + 0x2000 - (len + 1) * 2 - (odd >> 1));
                for (int i = 0; i < len; ++i) a[i] = b[i] = (wchar_t)('a' + i % 26);
                a[len] = b[len] = 0;
                chk_all(a, b, len, "guard-both");
                /* one at the guard, the other in ordinary memory */
                wchar_t* c = far_ + (len & 7);
                memcpy(c, a, (len + 1) * 2);
                for (int f = 0; f < 4; ++f) { chk(f, a, c, -1, "guard-a"); chk(f, c, b, -1, "guard-b"); }
                if (len) { b[len - 1] = 'A' + (len - 1) % 26; chk_all(a, b, len, "guard-fold"); }
            }
        printf("  4. NOACCESS after both terminators:  %lld fails\n", fails - f0);

        /* 5 */
        f0 = fails;
        for (int odd = 0; odd < 2; ++odd)
            for (int len = 1; len <= 160; ++len) {
                wchar_t* a = (wchar_t*)(pa + 0x2000 - len * 2 - odd);
                for (int i = 0; i < len; ++i) { ((unsigned char*)a)[2 * i] = (unsigned char)('a' + i % 26); ((unsigned char*)a)[2 * i + 1] = 0; }
                if (odd) ((unsigned char*)a)[2 * len] = 'q';
                static wchar_t e[400];
                for (int i = 0; i < len; ++i) e[i] = (wchar_t)('a' + i % 26);
                e[len] = (wchar_t)'z'; e[len + 1] = 0;          /* equal up to the page end */
                for (int f = 0; f < 4; ++f) {
                    int r1 = 0, r2 = 0;
                    int f1 = faults(f, 0, a, e, -1, &r1), f2 = faults(f, 1, a, e, -1, &r2);
                    int g1 = faults(f, 0, e, a, -1, &r1), g2 = faults(f, 1, e, a, -1, &r2);
                    tested += 2;
                    if (!f1 || !f2 || !g1 || !g2) { if (fails < 20) printf("FAIL %s unterminated-equal len=%d odd=%d faults sys %d%d ours %d%d\n", NM[f], len, odd, f1, g1, f2, g2); ++fails; }
                    /* unequal before the page: no fault, same value */
                    int p = (int)(rnd() % len);
                    wchar_t save = e[p]; e[p] = (wchar_t)(save == 'q' ? 'r' : 'q');
                    f1 = faults(f, 0, a, e, -1, &r1); f2 = faults(f, 1, a, e, -1, &r2);
                    ++tested;
                    if (f1 || f2 || r1 != r2) { if (fails < 20) printf("FAIL %s unterminated-unequal len=%d p=%d faults %d %d values %d %d\n", NM[f], len, p, f1, f2, r1, r2); ++fails; }
                    e[p] = save;
                }
                /* 6. N forms: index n is the first unreadable unit */
                for (int f = 2; f < 4; ++f) {
                    int r1 = 0, r2 = 0;
                    int f1 = faults(f, 0, a, e, len, &r1), f2 = faults(f, 1, a, e, len, &r2);
                    ++tested;
                    if (f1 || f2 || r1 != r2) { if (fails < 20) printf("FAIL %s n=len at the page len=%d odd=%d faults %d %d values %d %d\n", NM[f], len, odd, f1, f2, r1, r2); ++fails; }
                    f1 = faults(f, 0, e, a, len, &r1); f2 = faults(f, 1, e, a, len, &r2);
                    ++tested;
                    if (f1 || f2 || r1 != r2) { if (fails < 20) printf("FAIL %s n=len swapped len=%d odd=%d\n", NM[f], len, odd); ++fails; }
                }
            }
        printf("  5+6. unterminated / n at the page:   %lld fails\n", fails - f0);
    }

    /* 7 */
    {
        long long f0 = fails;
        int r1 = 0, r2 = 0;
        for (int f = 0; f < 4; ++f) {
            int f1 = faults(f, 0, NULL, L"a", 1, &r1), f2 = faults(f, 1, NULL, L"a", 1, &r2);
            int g1 = faults(f, 0, L"a", NULL, 1, &r1), g2 = faults(f, 1, L"a", NULL, 1, &r2);
            tested += 2;
            if (f1 != f2 || g1 != g2 || !f1 || !g1) { printf("FAIL %s NULL faults sys %d%d ours %d%d\n", NM[f], f1, g1, f2, g2); ++fails; }
        }
        for (int f = 2; f < 4; ++f) {
            int f1 = faults(f, 0, NULL, NULL, 0, &r1), f2 = faults(f, 1, NULL, NULL, 0, &r2);
            ++tested;
            if (f1 || f2 || r1 != r2 || r1 != 0) { printf("FAIL %s NULL n=0\n", NM[f]); ++fails; }
        }
        printf("  7. NULL arguments:                   %lld fails\n", fails - f0);
    }

    if (!fails) printf("CORRECTNESS: PASS (StrCmpCW/ICW/NCW/NICW vs live shlwapi + oracle, exact values, %lld cases: every unit vs a probe set, every pair below 0x180, lengths x odd offsets x every position, fold-only equality, n incl. negative, NOACCESS guards, fault iff equal up to an unreadable page, n at the page, NULLs)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
