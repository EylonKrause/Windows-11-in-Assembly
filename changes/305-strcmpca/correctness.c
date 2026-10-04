/* changes/305-strcmpca/correctness.c
 * Gate 1: wia_strcmp{c,ic,nc,nic}a against live shlwapi!StrCmpCA / StrCmpICA / StrCmpNCA / StrCmpNICA
 * and the scalar oracle, comparing the exact return value -- the difference, with the C forms on
 * unsigned bytes and the I forms on signed chars.
 *
 *   1. one-byte strings: every byte pair, all four forms, n = 1 and n = -1
 *   2. strings of 0..200 bytes at every byte offset 0..33 for a and a spread of offsets for b, equal,
 *      unequal at every position (including bytes >= 0x80 on either side), or equal under the fold only;
 *      N forms with n around every interesting index, and negative
 *   3. random strings over a small alphabet of case pairs, their neighbours and high bytes
 *   4. both strings ending as the last byte before a NOACCESS page, at every length and offset
 *   5. unterminated strings running into NOACCESS: equal up to the page -> both must fault;
 *      unequal before it -> neither may fault, and the values must agree
 *   6. N forms whose index n is the first unreadable byte: no fault
 *   7. NULL: n == 0 returns 0; otherwise both fault
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef int (WINAPI *PCMP)(PCSTR, PCSTR);
typedef int (WINAPI *PCMPN)(PCSTR, PCSTR, int);
extern int wia_strcmpca(PCSTR, PCSTR);
extern int wia_strcmpica(PCSTR, PCSTR);
extern int wia_strcmpnca(PCSTR, PCSTR, int);
extern int wia_strcmpnica(PCSTR, PCSTR, int);
int ref_cmpa(const char*, const char*, int, int, int);

static PCMP sCA, sICA; static PCMPN sNCA, sNICA;
static long long tested, fails;
static const char* NM[4] = { "StrCmpCA", "StrCmpICA", "StrCmpNCA", "StrCmpNICA" };

static int call_sys(int f, const char* a, const char* b, int n) {
    switch (f) { case 0: return sCA(a, b); case 1: return sICA(a, b); case 2: return sNCA(a, b, n); default: return sNICA(a, b, n); }
}
static int call_ours(int f, const char* a, const char* b, int n) {
    switch (f) { case 0: return wia_strcmpca(a, b); case 1: return wia_strcmpica(a, b); case 2: return wia_strcmpnca(a, b, n); default: return wia_strcmpnica(a, b, n); }
}

static void chk(int f, const char* a, const char* b, int n, const char* what) {
    int rs = call_sys(f, a, b, n), ro = call_ours(f, a, b, n), rr = ref_cmpa(a, b, f & 1, f >> 1, n);
    ++tested;
    if (rs != ro || rs != rr) {
        if (fails < 20) printf("FAIL %s %s n=%d: sys %d ours %d ref %d\n", NM[f], what, n, rs, ro, rr);
        ++fails;
    }
}
static void chk_all(const char* a, const char* b, int len, const char* what) {
    chk(0, a, b, 0, what); chk(1, a, b, 0, what);
    int ns[] = { 0, 1, 2, 3, 4, 5, 15, 16, 19, 20, 21, 31, 32, 33, 35, 36, len - 1, len, len + 1, len + 40, -1, -7, 0x7FFFFFFF, (int)0x80000000 };
    for (int i = 0; i < (int)(sizeof ns / sizeof ns[0]); ++i) { chk(2, a, b, ns[i], what); chk(3, a, b, ns[i], what); }
}

static unsigned rng = 12345u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static int faults(int f, int ours, const char* a, const char* b, int n, int* r) {
    __try { *r = ours ? call_ours(f, a, b, n) : call_sys(f, a, b, n); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sCA = (PCMP)GetProcAddress(h, "StrCmpCA");   sICA = (PCMP)GetProcAddress(h, "StrCmpICA");
    sNCA = (PCMPN)GetProcAddress(h, "StrCmpNCA"); sNICA = (PCMPN)GetProcAddress(h, "StrCmpNICA");
    if (!sCA || !sICA || !sNCA || !sNICA) { printf("missing export\n"); return 2; }

    /* 1 */
    {
        char a[2] = { 0 }, b[2] = { 0 };
        for (int x = 0; x < 256; ++x)
            for (int y = 0; y < 256; ++y) {
                a[0] = (char)x; b[0] = (char)y;
                for (int f = 0; f < 4; ++f) { chk(f, a, b, 1, "pair"); chk(f, a, b, -1, "pair"); }
            }
        printf("  1. every byte pair:                  %lld fails\n", fails);
    }

    /* 2 */
    {
        long long f0 = fails;
        static char A[1024], B[1024];
        static const int BO[] = { 0, 1, 2, 3, 7, 15, 17, 31, 33 };
        for (int len = 0; len <= 200; len += (len < 48 ? 1 : 7))
            for (int ao = 0; ao < 34; ao += (len < 48 ? 1 : 5))
                for (int bi = 0; bi < (int)(sizeof BO / sizeof BO[0]); ++bi) {
                    char* a = A + 64 + ao;
                    char* b = B + 64 + BO[bi];
                    for (int i = 0; i < len; ++i) { unsigned r = rnd(); a[i] = (char)((r & 7) ? 'a' + r % 26 : 1 + (r >> 4) % 255); }
                    a[len] = 0;
                    memcpy(b, a, len + 1);
                    chk_all(a, b, len, "equal");
                    for (int p = 0; p <= len; ++p) {
                        char save = b[p];
                        unsigned r = rnd();
                        unsigned char v = (r & 1) ? (unsigned char)(save + 1 + (r >> 1) % 3) : (unsigned char)(save ? save - 1 : 0x41);
                        if (r % 5 == 0) v = 0;
                        if (r % 7 == 0) v = (unsigned char)(0x80 + (r >> 3) % 128);
                        if ((char)v == save) v ^= 1;
                        b[p] = (char)v;
                        chk_all(a, b, len, "unequal");
                        chk_all(b, a, len, "unequal-swapped");
                        b[p] = save;
                    }
                    for (int i = 0; i < len; ++i) if (b[i] >= 'a' && b[i] <= 'z' && (rnd() & 1)) b[i] -= 32;
                    chk_all(a, b, len, "fold-equal");
                }
        printf("  2. strings x offsets x positions:    %lld fails\n", fails - f0);
    }

    /* 3 */
    {
        long long f0 = fails;
        static const unsigned char AL[] = { 'a', 'A', 'z', 'Z', '[', '`', '@', '{', 0xC0, 0xE0, 0x80, 0xFF };
        static char A[600], B[600];
        for (int t = 0; t < 80000; ++t) {
            int la = rnd() % 90, lb = rnd() % 90;
            char* a = A + 64 + rnd() % 34;
            char* b = B + 64 + rnd() % 34;
            for (int i = 0; i < la; ++i) a[i] = (char)AL[rnd() % 12];
            a[la] = 0;
            int share = rnd() % (la + 1);
            for (int i = 0; i < lb; ++i) b[i] = (i < share && i < la) ? ((rnd() % 3) ? a[i] : (char)AL[rnd() % 12]) : (char)AL[rnd() % 12];
            b[lb] = 0;
            for (int f = 0; f < 4; ++f) chk(f, a, b, (int)(rnd() % 100) - 3, "fuzz");
        }
        printf("  3. small-alphabet fuzz:              %lld fails\n", fails - f0);
    }

    /* 4, 5, 6 */
    {
        long long f0 = fails;
        unsigned char* pa = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        unsigned char* pb = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(pa, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(pb, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        static char far_[700];
        for (int len = 0; len <= 300; ++len)
            for (int sh = 0; sh < 3; ++sh) {
                char* a = (char*)(pa + 0x2000 - (len + 1));
                char* b = (char*)(pb + 0x2000 - (len + 1) - sh);     /* b's terminator sh bytes before its page end */
                for (int i = 0; i < len; ++i) a[i] = b[i] = (char)('a' + i % 26);
                a[len] = b[len] = 0;
                chk_all(a, b, len, "guard-both");
                char* c = far_ + (len & 7);
                memcpy(c, a, len + 1);
                for (int f = 0; f < 4; ++f) { chk(f, a, c, -1, "guard-a"); chk(f, c, b, -1, "guard-b"); }
                if (len) { b[len - 1] = (char)('A' + (len - 1) % 26); chk_all(a, b, len, "guard-fold"); }
            }
        printf("  4. NOACCESS after both terminators:  %lld fails\n", fails - f0);

        f0 = fails;
        for (int len = 1; len <= 200; ++len) {
            char* a = (char*)(pa + 0x2000 - len);                     /* unterminated: ends at the page */
            for (int i = 0; i < len; ++i) a[i] = (char)('a' + i % 26);
            static char e[400];
            for (int i = 0; i < len; ++i) e[i] = (char)('a' + i % 26);
            e[len] = 'z'; e[len + 1] = 0;
            for (int f = 0; f < 4; ++f) {
                int r1 = 0, r2 = 0;
                int f1 = faults(f, 0, a, e, -1, &r1), f2 = faults(f, 1, a, e, -1, &r2);
                int g1 = faults(f, 0, e, a, -1, &r1), g2 = faults(f, 1, e, a, -1, &r2);
                tested += 2;
                if (!f1 || !f2 || !g1 || !g2) { if (fails < 20) printf("FAIL %s unterminated-equal len=%d faults sys %d%d ours %d%d\n", NM[f], len, f1, g1, f2, g2); ++fails; }
                int p = (int)(rnd() % len);
                char save = e[p]; e[p] = (char)(save == 'q' ? 'r' : 'q');
                f1 = faults(f, 0, a, e, -1, &r1); f2 = faults(f, 1, a, e, -1, &r2);
                ++tested;
                if (f1 || f2 || r1 != r2) { if (fails < 20) printf("FAIL %s unterminated-unequal len=%d p=%d faults %d %d values %d %d\n", NM[f], len, p, f1, f2, r1, r2); ++fails; }
                e[p] = save;
            }
            for (int f = 2; f < 4; ++f) {
                int r1 = 0, r2 = 0;
                int f1 = faults(f, 0, a, e, len, &r1), f2 = faults(f, 1, a, e, len, &r2);
                ++tested;
                if (f1 || f2 || r1 != r2) { if (fails < 20) printf("FAIL %s n=len at the page len=%d faults %d %d values %d %d\n", NM[f], len, f1, f2, r1, r2); ++fails; }
                f1 = faults(f, 0, e, a, len, &r1); f2 = faults(f, 1, e, a, len, &r2);
                ++tested;
                if (f1 || f2 || r1 != r2) { if (fails < 20) printf("FAIL %s n=len swapped len=%d\n", NM[f], len); ++fails; }
            }
        }
        printf("  5+6. unterminated / n at the page:   %lld fails\n", fails - f0);
    }

    /* 7 */
    {
        long long f0 = fails;
        int r1 = 0, r2 = 0;
        for (int f = 0; f < 4; ++f) {
            int f1 = faults(f, 0, NULL, "a", 1, &r1), f2 = faults(f, 1, NULL, "a", 1, &r2);
            int g1 = faults(f, 0, "a", NULL, 1, &r1), g2 = faults(f, 1, "a", NULL, 1, &r2);
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

    if (!fails) printf("CORRECTNESS: PASS (StrCmpCA/ICA/NCA/NICA vs live shlwapi + oracle, exact values, %lld cases: every byte pair, lengths x offsets x every position incl. high bytes, fold-only equality, n incl. negative, NOACCESS guards, fault iff equal up to an unreadable page, n at the page, NULLs)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
