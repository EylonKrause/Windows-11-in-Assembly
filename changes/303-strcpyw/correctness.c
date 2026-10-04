/* changes/303-strcpyw/correctness.c
 * Gate 1: wia_strcpyw / wia_strcatw against live shlwapi!StrCpyW / StrCatW and the scalar oracle,
 * comparing return values and the WHOLE poisoned arena, so a byte written before the destination or
 * past its terminator fails as loudly as a wrong character.
 *
 *   1. lengths 0..260 x source byte offsets 0..33 x destination byte offsets 0..33, ODD ones included
 *   2. StrCatW: destination lengths 0..80 x source lengths 0..80 x offsets
 *   3. the source terminator as the last wchar before NOACCESS, at every page offset of the start --
 *      the block loads must never read into the next page
 *   4. an UNTERMINATED source running into NOACCESS: both must fault, and the destination must hold
 *      exactly the same characters afterwards -- every one that was readable, written before the fault
 *   5. an unterminated StrCatW destination: both fault, nothing written
 *   6. a destination overlapping just BELOW the source, every gap 1..40 chars x lengths 0..200
 *   7. NULL destination / NULL source
 *   8. a 1M-character string
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef PWSTR (WINAPI *PFN)(PWSTR, PCWSTR);
extern PWSTR wia_strcpyw(PWSTR, PCWSTR);
extern PWSTR wia_strcatw(PWSTR, PCWSTR);
wchar_t* ref_strcpyw(wchar_t*, const wchar_t*);
wchar_t* ref_strcatw(wchar_t*, const wchar_t*);

static PFN sysCpy, sysCat;
static long long tested, fails;

#define AR 2048
static unsigned char SA[AR], DA[AR], DB[AR], DC[AR];

static void fill(wchar_t* s, int n, unsigned seed) {
    for (int i = 0; i < n; ++i) { seed = seed * 1103515245u + 12345u; s[i] = (wchar_t)(1 + (seed >> 9) % ((i & 3) ? 0x7F : 0xFFFE)); }
}

static void chk_cpy(int which, int dlen, int slen, int doff, int soff, unsigned seed) {
    /* source arena */
    memset(SA, 0x5A, AR);
    wchar_t* s = (wchar_t*)(SA + 64 + soff);
    wchar_t tmp[700]; fill(tmp, slen, seed); memcpy(s, tmp, slen * 2); s[slen] = 0;
    /* three destination arenas, identical */
    memset(DA, 0xA5, AR);
    wchar_t* da = (wchar_t*)(DA + 64 + doff);
    if (which) { wchar_t pre[200]; fill(pre, dlen, seed ^ 0x55u); memcpy(da, pre, dlen * 2); da[dlen] = 0; }
    memcpy(DB, DA, AR); memcpy(DC, DA, AR);
    wchar_t* db = (wchar_t*)(DB + 64 + doff); wchar_t* dc = (wchar_t*)(DC + 64 + doff);
    PWSTR ra = which ? sysCat(da, s) : sysCpy(da, s);
    PWSTR rb = which ? wia_strcatw(db, s) : wia_strcpyw(db, s);
    wchar_t* rc = which ? ref_strcatw(dc, s) : ref_strcpyw(dc, s);
    ++tested;
    if (ra != da || rb != db || rc != dc || memcmp(DA, DB, AR) || memcmp(DA, DC, AR)) {
        if (fails < 15) {
            int i = 0; while (i < AR && DA[i] == DB[i] && DA[i] == DC[i]) ++i;
            printf("FAIL %s dlen=%d slen=%d doff=%d soff=%d ret %d%d%d first diff byte %d: sys %02X ours %02X ref %02X\n",
                   which ? "StrCatW" : "StrCpyW", dlen, slen, doff, soff, ra == da, rb == db, rc == dc, i, DA[i], DB[i], DC[i]);
        }
        ++fails;
    }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sysCpy = (PFN)GetProcAddress(h, "StrCpyW");
    sysCat = (PFN)GetProcAddress(h, "StrCatW");
    if (!sysCpy || !sysCat) { printf("missing export\n"); return 2; }

    /* 1 */
    for (int slen = 0; slen <= 260; ++slen)
        for (int soff = 0; soff < 34; ++soff)
            for (int doff = 0; doff < 34; ++doff)
                chk_cpy(0, 0, slen, doff, soff, (unsigned)(slen * 977 + soff * 31 + doff));
    printf("  1. StrCpyW lengths x offsets:        %lld fails\n", fails);
    /* 2 */
    { long long f0 = fails;
      for (int dlen = 0; dlen <= 80; ++dlen)
        for (int slen = 0; slen <= 80; ++slen)
            for (int off = 0; off < 34; off += 3)
                chk_cpy(1, dlen, slen, off, (off * 7) % 34, (unsigned)(dlen * 131 + slen * 7 + off));
      printf("  2. StrCatW lengths x offsets:        %lld fails\n", fails - f0); }

    /* 3. terminator as the last wchar before NOACCESS */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, si.dwPageSize * 3, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, si.dwPageSize * 2, MEM_COMMIT, PAGE_READWRITE);
        unsigned char* end = b + si.dwPageSize * 2;
        static wchar_t d1[700], d2[700];
        long long f0 = fails;
        for (int odd = 0; odd < 2; ++odd)
            for (int n = 0; n <= 300; ++n) {
                wchar_t* s = (wchar_t*)(end - (n + 1) * 2 - odd);
                wchar_t tmp[400]; fill(tmp, n, (unsigned)n); memcpy(s, tmp, n * 2); s[n] = 0;
                memset(d1, 0xA5, sizeof d1); memset(d2, 0xA5, sizeof d2);
                ref_strcpyw(d1, s);
                wia_strcpyw(d2, s);                     /* one byte of over-read and this AVs */
                ++tested;
                if (memcmp(d1, d2, sizeof d1)) { if (fails < 15) printf("FAIL guard n=%d odd=%d\n", n, odd); ++fails; }
            }
        printf("  3. NOACCESS after the terminator:    %lld fails\n", fails - f0);
    }

    /* 4. unterminated source into NOACCESS: same fault, same characters written */
    {
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t d1[600], d2[600];
        long long f0 = fails;
        for (int odd = 0; odd < 2; ++odd)
            for (int n = 1; n <= 200; ++n) {
                wchar_t* s = (wchar_t*)(b + 0x2000 - n * 2 - odd);
                for (int i = 0; i < n; ++i) { ((unsigned char*)s)[2*i] = (unsigned char)('a' + i % 26); ((unsigned char*)s)[2*i+1] = 0; }
                if (odd) ((unsigned char*)s)[2*n] = 'q';
                memset(d1, 0xA5, sizeof d1); memset(d2, 0xA5, sizeof d2);
                int f1 = 0, f2 = 0;
                __try { sysCpy(d1, s); } __except (EXCEPTION_EXECUTE_HANDLER) { f1 = 1; }
                __try { wia_strcpyw(d2, s); } __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
                ++tested;
                if (!f1 || !f2 || memcmp(d1, d2, sizeof d1)) {
                    if (fails < 15) printf("FAIL unterminated n=%d odd=%d sysFault=%d oursFault=%d same=%d\n", n, odd, f1, f2, !memcmp(d1, d2, sizeof d1));
                    ++fails;
                }
            }
        printf("  4. unterminated source, fault point: %lld fails\n", fails - f0);
    }

    /* 5. unterminated StrCatW destination */
    {
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        long long f0 = fails;
        for (int n = 1; n <= 120; ++n)
            for (int side = 0; side < 2; ++side) {
                wchar_t* d = (wchar_t*)(b + 0x2000) - n;
                for (int i = 0; i < n; ++i) d[i] = L'x';
                int f = 0;
                __try { if (side) wia_strcatw(d, L"abc"); else sysCat(d, L"abc"); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
                int same = 1; for (int i = 0; i < n; ++i) if (d[i] != L'x') same = 0;
                ++tested;
                if (!f || !same) { if (fails < 15) printf("FAIL %s unterminated cat dst n=%d fault=%d unchanged=%d\n", side ? "OURS" : "sys", n, f, same); ++fails; }
            }
        printf("  5. unterminated StrCatW destination: %lld fails\n", fails - f0);
    }

    /* 6. destination overlapping just below the source */
    {
        static wchar_t A1[800], A2[800];
        long long f0 = fails;
        for (int g = 1; g <= 40; ++g)
            for (int n = 0; n <= 200; ++n)
                for (int al = 0; al < 2; ++al) {
                    wchar_t tmp[300]; fill(tmp, n, (unsigned)(g * 1000 + n));
                    for (int i = 0; i < 800; ++i) A1[i] = A2[i] = (wchar_t)0xBEEF;
                    memcpy(A1 + 100 + al, tmp, n * 2); A1[100 + al + n] = 0;
                    memcpy(A2 + 100 + al, tmp, n * 2); A2[100 + al + n] = 0;
                    sysCpy(A1 + 100 + al - g, A1 + 100 + al);
                    wia_strcpyw(A2 + 100 + al - g, A2 + 100 + al);
                    ++tested;
                    if (memcmp(A1, A2, sizeof A1)) { if (fails < 15) printf("FAIL overlap g=%d n=%d\n", g, n); ++fails; }
                }
        printf("  6. overlap below the source:         %lld fails\n", fails - f0);
    }

    /* 7. NULLs */
    {
        wchar_t d[8] = L"zz";
        long long f0 = fails;
        tested += 4;
        if (wia_strcpyw(NULL, L"abc") != sysCpy(NULL, L"abc")) ++fails;
        if (wia_strcpyw(d, NULL) != d || wcscmp(d, L"zz")) ++fails;
        if (wia_strcatw(NULL, L"abc") != sysCat(NULL, L"abc")) ++fails;
        if (wia_strcatw(d, NULL) != d || wcscmp(d, L"zz")) ++fails;
        printf("  7. NULL arguments:                   %lld fails\n", fails - f0);
    }

    /* 8. 1M characters */
    {
        size_t n = 1u << 20;
        wchar_t* s = (wchar_t*)malloc((n + 1) * 2); wchar_t* d1 = (wchar_t*)malloc((n + 64) * 2); wchar_t* d2 = (wchar_t*)malloc((n + 64) * 2);
        fill(s, (int)n, 7u); s[n] = 0;
        memset(d1, 0xA5, (n + 64) * 2); memset(d2, 0xA5, (n + 64) * 2);
        sysCpy(d1, s); wia_strcpyw(d2, s);
        ++tested;
        if (memcmp(d1, d2, (n + 64) * 2)) { printf("FAIL 1M\n"); ++fails; }
        free(s); free(d1); free(d2);
        printf("  8. 1M characters:                    %lld fails\n", fails);
    }

    if (!fails) printf("CORRECTNESS: PASS (StrCpyW + StrCatW vs live shlwapi + oracle, %lld cases: lengths x odd offsets, NOACCESS guards, the same fault point and written prefix on an unterminated source, overlap below the source, NULLs, 1M chars)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
