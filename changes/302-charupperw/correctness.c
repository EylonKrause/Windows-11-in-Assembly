/* changes/302-charupperw/correctness.c
 * Gate 1: wia_charupperw / wia_charlowerw against the live user32!CharUpperW / CharLowerW AND the
 * ntdll-based oracle in reference.c, comparing return values and the WHOLE arena around the string,
 * so a write past the terminator or before the start fails exactly as loudly as a wrong character.
 *
 *   0. the tables were built from the exports and satisfy the range rule below 0x80
 *   1. character mode, all 65536 values, both directions
 *   2. string mode over every non-NUL code unit at once
 *   3. lengths 0..300 at every byte offset 0..63 -- ODD offsets included, which take the scalar
 *      length scan because their wchars straddle the vector lanes -- over four contents: ASCII lower,
 *      ASCII upper, ASCII with one high unit planted at a moving position, and arbitrary units
 *   4. a 1M-character string, so every offset is 64-bit
 *   5. the terminator as the last wchar before a NOACCESS page, even and odd placements: pass 1's
 *      aligned loads must never read past it
 *   6. an UNTERMINATED string running into NOACCESS: the export faults with the buffer unchanged,
 *      because it measures before it writes, and so must this
 *   7. strings placed at 0x1'0000'0000 and above, whose low dword has a zero high word: still strings
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef LPWSTR (WINAPI *PFN)(LPWSTR);
extern LPWSTR wia_charupperw(LPWSTR);
extern LPWSTR wia_charlowerw(LPWSTR);
LPWSTR ref_charupperw(LPWSTR);
LPWSTR ref_charlowerw(LPWSTR);
extern int wia_cuw_init(void);

typedef struct { const char* name; PFN sys; LPWSTR (*ours)(LPWSTR); LPWSTR (*ref)(LPWSTR); } FN;
static FN F[2];
static long long tested, fails;

#define PAD   64
#define MAXB  (2 * 1024 + 256)
static unsigned char A[PAD + MAXB + PAD], B[PAD + MAXB + PAD], C[PAD + MAXB + PAD];

static void chk_str(int d, const wchar_t* content, size_t n, size_t off, const char* tag) {
    size_t total = PAD + MAXB + PAD;
    memset(A, 0xA5, total); memset(B, 0xA5, total); memset(C, 0xA5, total);
    unsigned char* pa = A + PAD + off; unsigned char* pb = B + PAD + off; unsigned char* pc = C + PAD + off;
    memcpy(pa, content, n * 2); memset(pa + n * 2, 0, 2);
    memcpy(pb, content, n * 2); memset(pb + n * 2, 0, 2);
    memcpy(pc, content, n * 2); memset(pc + n * 2, 0, 2);
    LPWSTR ra = F[d].sys((LPWSTR)pa), rb = F[d].ours((LPWSTR)pb), rc = F[d].ref((LPWSTR)pc);
    ++tested;
    int ok = (ra == (LPWSTR)pa) && (rb == (LPWSTR)pb) && (rc == (LPWSTR)pc)
          && !memcmp(A, B, total) && !memcmp(A, C, total);
    if (!ok) {
        if (fails < 15) {
            size_t i = 0; while (i < total && A[i] == B[i] && A[i] == C[i]) ++i;
            printf("FAIL %s [%s] n=%zu off=%zu ret %s/%s/%s, first diff at arena[%zu]: sys %02X ours %02X ref %02X\n",
                   F[d].name, tag, n, off, ra == (LPWSTR)pa ? "ok" : "BAD", rb == (LPWSTR)pb ? "ok" : "BAD",
                   rc == (LPWSTR)pc ? "ok" : "BAD", i, i < total ? A[i] : 0, i < total ? B[i] : 0, i < total ? C[i] : 0);
        }
        ++fails;
    }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"user32.dll");
    F[0].name = "CharUpperW"; F[0].sys = (PFN)GetProcAddress(u, "CharUpperW"); F[0].ours = wia_charupperw; F[0].ref = ref_charupperw;
    F[1].name = "CharLowerW"; F[1].sys = (PFN)GetProcAddress(u, "CharLowerW"); F[1].ours = wia_charlowerw; F[1].ref = ref_charlowerw;
    if (!F[0].sys || !F[1].sys) { printf("missing export\n"); return 2; }

    int r = wia_cuw_init();
    if (r) { printf("the case tables failed to build (%d)\n", r); return 1; }
    printf("  0. tables built from the exports, range rule below 0x80 holds\n");

    /* 1. character mode */
    for (int d = 0; d < 2; ++d)
        for (UINT_PTR v = 0; v < 0x10000; ++v) {
            LPWSTR a = F[d].sys((LPWSTR)v), b = F[d].ours((LPWSTR)v), c = F[d].ref((LPWSTR)v);
            ++tested;
            if (a != b || a != c) { if (fails < 15) printf("FAIL %s char mode %04X: sys %p ours %p ref %p\n", F[d].name, (unsigned)v, (void*)a, (void*)b, (void*)c); ++fails; }
        }
    printf("  1. character mode, 2 x 65536:          %lld fails\n", fails);

    /* 2. every non-NUL code unit in one string */
    {
        static wchar_t all[65536], a2[65536], b2[65536], c2[65536];
        for (int i = 1; i < 65536; ++i) all[i - 1] = (wchar_t)i;
        all[65535] = 0;
        for (int d = 0; d < 2; ++d) {
            memcpy(a2, all, sizeof all); memcpy(b2, all, sizeof all); memcpy(c2, all, sizeof all);
            F[d].sys(a2); F[d].ours(b2); F[d].ref(c2);
            ++tested;
            if (memcmp(a2, b2, sizeof all) || memcmp(a2, c2, sizeof all)) { printf("FAIL %s all-units string\n", F[d].name); ++fails; }
        }
        printf("  2. every code unit in string mode:     %lld fails\n", fails);
    }

    /* 3. lengths x byte offsets x contents */
    {
        static wchar_t s[1100];
        long long f0 = fails;
        unsigned long seed = 0x302u;
        for (int d = 0; d < 2; ++d)
            for (int kind = 0; kind < 4; ++kind)
                for (size_t n = 0; n <= 300; ++n)
                    for (size_t off = 0; off < 64; ++off) {
                        for (size_t i = 0; i < n; ++i) {
                            if (kind == 0)      s[i] = (wchar_t)(L'a' + i % 26);
                            else if (kind == 1) s[i] = (wchar_t)(L'A' + i % 26);
                            else if (kind == 2) s[i] = (i == (n * 7 + off) % (n ? n : 1)) ? (wchar_t)0x0430 : (wchar_t)(L'a' + i % 26);
                            else { seed = seed * 1103515245u + 12345u; s[i] = (wchar_t)(1 + (seed >> 8) % 0xFFFF); }
                        }
                        chk_str(d, s, n, off, "len x off");
                    }
        printf("  3. lengths 0..300 x offsets 0..63 x 4: %lld fails\n", fails - f0);
    }

    /* 4. a 1M-character string */
    {
        size_t n = 1u << 20;
        wchar_t* a = (wchar_t*)malloc((n + 1) * 2); wchar_t* b = (wchar_t*)malloc((n + 1) * 2); wchar_t* c = (wchar_t*)malloc((n + 1) * 2);
        for (size_t i = 0; i < n; ++i) a[i] = (wchar_t)((i % 1000 == 999) ? 0x00E9 : (L'a' + i % 26));
        a[n] = 0;
        for (int d = 0; d < 2; ++d) {
            memcpy(b, a, (n + 1) * 2); memcpy(c, a, (n + 1) * 2);
            wchar_t* x = (wchar_t*)malloc((n + 1) * 2); memcpy(x, a, (n + 1) * 2);
            F[d].sys(x); F[d].ours(b); F[d].ref(c);
            ++tested;
            if (memcmp(x, b, (n + 1) * 2) || memcmp(x, c, (n + 1) * 2)) { printf("FAIL %s 1M string\n", F[d].name); ++fails; }
            free(x);
        }
        free(a); free(b); free(c);
        printf("  4. 1M-character string:                %lld fails\n", fails);
    }

    /* 5. terminator as the last wchar before NOACCESS */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        unsigned char* base = (unsigned char*)VirtualAlloc(NULL, si.dwPageSize * 3, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(base, si.dwPageSize * 2, MEM_COMMIT, PAGE_READWRITE);
        unsigned char* end = base + si.dwPageSize * 2;
        static wchar_t ref[400], got[400];
        long long f0 = fails;
        for (int d = 0; d < 2; ++d)
            for (int odd = 0; odd < 2; ++odd)
                for (size_t n = 0; n <= 200; ++n) {
                    wchar_t* p = (wchar_t*)(end - (n + 1) * 2 - odd);
                    for (size_t i = 0; i < n; ++i) ((unsigned char*)p)[2*i] = (unsigned char)('a' + i % 26), ((unsigned char*)p)[2*i+1] = (i % 37 == 5) ? 0x04 : 0;
                    ((unsigned char*)p)[2*n] = 0; ((unsigned char*)p)[2*n+1] = 0;
                    memcpy(ref, p, (n + 1) * 2);
                    LPWSTR rr = F[d].ref(ref);
                    LPWSTR ro = F[d].ours(p);                  /* one byte of over-read and this AVs */
                    memcpy(got, p, (n + 1) * 2);
                    ++tested;
                    if (ro != p || memcmp(got, ref, (n + 1) * 2)) { if (fails < 15) printf("FAIL %s guard n=%zu odd=%d\n", F[d].name, n, odd); ++fails; }
                    (void)rr;
                }
        printf("  5. NOACCESS after the terminator:      %lld fails\n", fails - f0);
    }

    /* 6. unterminated into NOACCESS: fault, buffer unchanged, both of them */
    {
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        long long f0 = fails;
        for (int d = 0; d < 2; ++d)
            for (int n = 1; n <= 80; ++n)
                for (int odd = 0; odd < 2; ++odd) {
                    wchar_t* t = (wchar_t*)(b + 0x2000 - n * 2 - odd);
                    for (int side = 0; side < 2; ++side) {
                        for (int i = 0; i < n; ++i) { ((unsigned char*)t)[2*i] = (unsigned char)('a' + i % 26); ((unsigned char*)t)[2*i+1] = 0; }
                        if (odd) ((unsigned char*)t)[2*n] = 'q';           /* the straddling byte, non-zero */
                        int faulted = 0;
                        __try { if (side) F[d].ours(t); else F[d].sys(t); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
                        int untouched = 1;
                        for (int i = 0; i < n; ++i) if (((unsigned char*)t)[2*i] != (unsigned char)('a' + i % 26)) untouched = 0;
                        ++tested;
                        if (!faulted || !untouched) {
                            if (fails < 15) printf("FAIL %s %s unterminated n=%d odd=%d: faulted=%d untouched=%d\n", F[d].name, side ? "OURS" : "sys", n, odd, faulted, untouched);
                            ++fails;
                        }
                    }
                }
        printf("  6. unterminated into NOACCESS:         %lld fails\n", fails - f0);
    }

    /* 7. strings above 4 GB whose low dword has a zero high word */
    {
        static const UINT_PTR AT[] = { 0x100000000ull, 0x200000000ull, 0x7FF000000000ull };
        long long f0 = fails;
        for (int k = 0; k < 3; ++k) {
            wchar_t* m = (wchar_t*)VirtualAlloc((void*)AT[k], 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!m) continue;
            for (int d = 0; d < 2; ++d)
                for (int o = 0; o < 0x40; o += 1) {
                    wchar_t ref[8];
                    wcscpy(m + o, L"abcXYZ"); wcscpy(ref, L"abcXYZ");
                    F[d].ref(ref);
                    LPWSTR r2 = F[d].ours(m + o);
                    ++tested;
                    if (r2 != m + o || wcscmp(m + o, ref)) { if (fails < 15) printf("FAIL %s at %p\n", F[d].name, (void*)(m + o)); ++fails; }
                }
            VirtualFree(m, 0, MEM_RELEASE);
        }
        printf("  7. strings at 0x1'0000'0000 and up:    %lld fails\n", fails - f0);
    }

    if (!fails) printf("CORRECTNESS: PASS (CharUpperW + CharLowerW vs live user32 + ntdll oracle, %lld cases: both modes, every code unit, odd offsets, NOACCESS guards, unterminated-fault semantics, >4 GB pointers)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
