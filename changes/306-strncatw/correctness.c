/* changes/306-strncatw/correctness.c
 * Gate 1: wia_strncatw against live shlwapi!StrNCatW and the scalar oracle, comparing the return value
 * and the WHOLE poisoned arena, so a byte written before dst or past the new terminator fails as loudly
 * as a wrong character.
 *
 *   1. dst lengths 0..40 x src lengths 0..80 x every n from -3 to slen+3 x odd and even offsets
 *   2. random longer cases: dst and src up to 300, random n
 *   3. overlap: src inside dst's string, so the write cursor is 1..40 characters above it (the export's
 *      forward loop re-reads what it wrote), at even and odd byte distances; and dst below src
 *   4. the src terminator as the last wchar before NOACCESS, every length, even and odd placement
 *   5. an unterminated src running into NOACCESS: with a large n both fault after writing every readable
 *      character; with src[n-1] the first unreadable unit both fault after n-1 characters and no NUL;
 *      with src[n-1] the last readable one neither faults
 *   6. an unterminated dst: both fault, nothing written
 *   7. NULLs; n < 0 and n == 0 on a READ-ONLY dst (the export writes the NUL again for n < 0)
 *   8. 1M characters
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef PWSTR (WINAPI *PFN)(PWSTR, PCWSTR, int);
extern PWSTR wia_strncatw(PWSTR, PCWSTR, int);
wchar_t* ref_strncatw(wchar_t*, const wchar_t*, int);

static PFN sNCat;
static long long tested, fails;

#define AR 2048
static unsigned char SA[AR], DA[AR], DB[AR], DC[AR];
static unsigned rng = 777u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void fill(wchar_t* s, int n) { for (int i = 0; i < n; ++i) { unsigned r = rnd(); s[i] = (wchar_t)((r & 3) ? 'a' + r % 26 : 1 + (r >> 4) % 0xFFFE); } }

static void chk(int dlen, int slen, int n, int doff, int soff) {
    memset(SA, 0x5A, AR);
    wchar_t* s = (wchar_t*)(SA + 64 + soff);
    fill(s, slen); s[slen] = 0;
    memset(DA, 0xA5, AR);
    wchar_t* da = (wchar_t*)(DA + 64 + doff);
    fill(da, dlen); da[dlen] = 0;
    memcpy(DB, DA, AR); memcpy(DC, DA, AR);
    wchar_t* db = (wchar_t*)(DB + 64 + doff); wchar_t* dc = (wchar_t*)(DC + 64 + doff);
    PWSTR ra = sNCat(da, s, n), rb = wia_strncatw(db, s, n);
    wchar_t* rc = ref_strncatw(dc, s, n);
    ++tested;
    if (ra != da || rb != db || rc != dc || memcmp(DA, DB, AR) || memcmp(DA, DC, AR)) {
        if (fails < 15) {
            int i = 0; while (i < AR && DA[i] == DB[i] && DA[i] == DC[i]) ++i;
            printf("FAIL dlen=%d slen=%d n=%d doff=%d soff=%d ret %d%d%d first diff byte %d: sys %02X ours %02X ref %02X\n",
                   dlen, slen, n, doff, soff, ra == da, rb == db, rc == dc, i, DA[i], DB[i], DC[i]);
        }
        ++fails;
    }
}

static int faults(int ours, wchar_t* d, const wchar_t* s, int n) {
    __try { if (ours) wia_strncatw(d, s, n); else sNCat(d, s, n); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    sNCat = (PFN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrNCatW");
    if (!sNCat) { printf("missing export\n"); return 2; }

    /* 1 */
    for (int dlen = 0; dlen <= 40; ++dlen)
        for (int slen = 0; slen <= 80; ++slen)
            for (int n = -3; n <= slen + 3; ++n)
                chk(dlen, slen, n, (dlen * 7 + n) & 31, (slen * 5 + n * 3) & 31);
    printf("  1. lengths x every n x offsets:      %lld fails\n", fails);

    /* 2 */
    { long long f0 = fails;
      for (int t = 0; t < 60000; ++t) {
          int dlen = rnd() % 300, slen = rnd() % 300;
          int n = (int)(rnd() % 400) - 50;
          chk(dlen, slen, n, rnd() % 34, rnd() % 34);
      }
      printf("  2. random longer cases:              %lld fails\n", fails - f0); }

    /* 3. overlap */
    { long long f0 = fails;
      static unsigned char A1[4096], A2[4096], A3[4096];
      for (int dlen = 1; dlen <= 60; ++dlen)
          for (int back = 1; back <= dlen; ++back)           /* src = p - back characters */
              for (int odd = 0; odd < 2; ++odd)
                  for (int n = 1; n <= 90; n += (n < 40 ? 1 : 7)) {
                      memset(A1, 0x33, sizeof A1);
                      wchar_t* d1 = (wchar_t*)(A1 + 64 + odd);
                      fill(d1, dlen); d1[dlen] = 0;
                      memcpy(A2, A1, sizeof A1); memcpy(A3, A1, sizeof A1);
                      wchar_t* d2 = (wchar_t*)(A2 + 64 + odd); wchar_t* d3 = (wchar_t*)(A3 + 64 + odd);
                      /* odd byte distance too: src one byte further down */
                      for (int bb = 0; bb < 2; ++bb) {
                          if (bb) { memcpy(A2, A1, sizeof A1); memcpy(A3, A1, sizeof A1); }
                          unsigned char* src1 = (unsigned char*)(d1 + dlen - back) - bb;
                          unsigned char* src2 = (unsigned char*)(d2 + dlen - back) - bb;
                          unsigned char* src3 = (unsigned char*)(d3 + dlen - back) - bb;
                          unsigned char save[4096]; memcpy(save, A1, sizeof A1);
                          sNCat(d1, (wchar_t*)src1, n); wia_strncatw(d2, (wchar_t*)src2, n); ref_strncatw(d3, (wchar_t*)src3, n);
                          ++tested;
                          if (memcmp(A1, A2, sizeof A1) || memcmp(A1, A3, sizeof A1)) { if (fails < 15) printf("FAIL overlap dlen=%d back=%d odd=%d bb=%d n=%d\n", dlen, back, odd, bb, n); ++fails; }
                          memcpy(A1, save, sizeof A1);
                      }
                  }
      /* dst below src: src later in the same buffer, past dst's terminator */
      for (int dlen = 0; dlen <= 40; ++dlen)
          for (int gap = 1; gap <= 40; ++gap)
              for (int slen = 0; slen <= 60; slen += 3) {
                  memset(A1, 0x44, sizeof A1);
                  wchar_t* d1 = (wchar_t*)(A1 + 64);
                  fill(d1, dlen); d1[dlen] = 0;
                  wchar_t* s1 = d1 + dlen + gap; fill(s1, slen); s1[slen] = 0;
                  memcpy(A2, A1, sizeof A1);
                  wchar_t* d2 = (wchar_t*)(A2 + 64); wchar_t* s2 = d2 + dlen + gap;
                  int n = slen + 2;
                  sNCat(d1, s1, n); wia_strncatw(d2, s2, n);
                  ++tested;
                  if (memcmp(A1, A2, sizeof A1)) { if (fails < 15) printf("FAIL below dlen=%d gap=%d slen=%d\n", dlen, gap, slen); ++fails; }
              }
      printf("  3. overlapping source:               %lld fails\n", fails - f0); }

    /* 4, 5 */
    { long long f0 = fails;
      SYSTEM_INFO si; GetSystemInfo(&si);
      unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
      VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
      unsigned char* end = b + 0x2000;
      static wchar_t d1[800], d2[800];
      for (int odd = 0; odd < 2; ++odd)
          for (int len = 0; len <= 300; ++len) {
              wchar_t* s = (wchar_t*)(end - (len + 1) * 2 - odd);
              fill(s, len); s[len] = 0;
              int ns[] = { len + 1, len + 2, len + 1000, len, len > 0 ? len - 1 : 1 };
              for (int k = 0; k < 5; ++k) {
                  for (int i = 0; i < 800; ++i) d1[i] = d2[i] = (wchar_t)0xBEEF;
                  d1[3] = d2[3] = 0;
                  ref_strncatw(d1, s, ns[k]); wia_strncatw(d2, s, ns[k]);
                  ++tested;
                  if (memcmp(d1, d2, sizeof d1)) { if (fails < 15) printf("FAIL guard len=%d odd=%d n=%d\n", len, odd, ns[k]); ++fails; }
              }
          }
      printf("  4. NOACCESS after the src NUL:       %lld fails\n", fails - f0);
      f0 = fails;
      for (int odd = 0; odd < 2; ++odd)
          for (int len = 1; len <= 200; ++len) {
              wchar_t* s = (wchar_t*)(end - len * 2 - odd);
              for (int i = 0; i < len; ++i) { ((unsigned char*)s)[2 * i] = (unsigned char)('a' + i % 26); ((unsigned char*)s)[2 * i + 1] = 0; }
              if (odd) ((unsigned char*)s)[2 * len] = 'q';
              int ns[] = { 1000, len + 1, len, 1, 2 };
              for (int k = 0; k < 5; ++k) {
                  for (int i = 0; i < 800; ++i) d1[i] = d2[i] = (wchar_t)0xBEEF;
                  d1[2] = d2[2] = 0;
                  int f1 = faults(0, d1, s, ns[k]), f2 = faults(1, d2, s, ns[k]);
                  ++tested;
                  int expect = ns[k] > len;          /* src[n-1] or earlier is unreadable */
                  if (f1 != expect || f2 != expect || memcmp(d1, d2, sizeof d1)) {
                      if (fails < 15) printf("FAIL unterminated len=%d odd=%d n=%d faults sys %d ours %d same %d\n", len, odd, ns[k], f1, f2, !memcmp(d1, d2, sizeof d1));
                      ++fails;
                  }
              }
          }
      printf("  5. unterminated src, fault point:    %lld fails\n", fails - f0); }

    /* 6 */
    { long long f0 = fails;
      unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
      VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
      for (int n = 1; n <= 100; ++n)
          for (int side = 0; side < 2; ++side) {
              wchar_t* d = (wchar_t*)(b + 0x2000) - n;
              for (int i = 0; i < n; ++i) d[i] = L'x';
              int f = faults(side, d, L"abc", 10);
              int same = 1; for (int i = 0; i < n; ++i) if (d[i] != L'x') same = 0;
              ++tested;
              if (!f || !same) { if (fails < 15) printf("FAIL %s unterminated dst n=%d fault=%d unchanged=%d\n", side ? "OURS" : "sys", n, f, same); ++fails; }
          }
      printf("  6. unterminated dst:                 %lld fails\n", fails - f0); }

    /* 7 */
    { long long f0 = fails;
      wchar_t d[8] = L"zz";
      tested += 4;
      if (wia_strncatw(NULL, L"abc", 5) != sNCat(NULL, L"abc", 5)) ++fails;
      if (wia_strncatw(d, NULL, 5) != d || wcscmp(d, L"zz")) ++fails;
      if (wia_strncatw(NULL, NULL, 0) != NULL) ++fails;
      if (wia_strncatw(d, NULL, -1) != d) ++fails;
      /* read-only dst: n < 0 writes the NUL again (a fault), n == 0 writes nothing */
      wchar_t* ro = (wchar_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
      wcscpy(ro, L"read-only");
      DWORD old; VirtualProtect(ro, 0x1000, PAGE_READONLY, &old);
      int ns[] = { -1, -5, 0 };
      for (int k = 0; k < 3; ++k) {
          int f1 = faults(0, ro, L"xy", ns[k]), f2 = faults(1, ro, L"xy", ns[k]);
          ++tested;
          if (f1 != f2) { printf("FAIL read-only dst n=%d faults sys %d ours %d\n", ns[k], f1, f2); ++fails; }
      }
      printf("  7. NULLs, read-only dst with n <= 0:  %lld fails\n", fails - f0); }

    /* 8 */
    { size_t n = 1u << 20;
      wchar_t* s = (wchar_t*)malloc((n + 1) * 2); wchar_t* d1 = (wchar_t*)malloc((2 * n + 64) * 2); wchar_t* d2 = (wchar_t*)malloc((2 * n + 64) * 2);
      fill(s, (int)n); s[n] = 0;
      for (size_t i = 0; i < 2 * n + 64; ++i) d1[i] = d2[i] = (wchar_t)0xA5A5;
      fill(d1, 1000); d1[1000] = 0; memcpy(d2, d1, 1001 * 2);
      sNCat(d1, s, (int)n + 5); wia_strncatw(d2, s, (int)n + 5);
      ++tested;
      if (memcmp(d1, d2, (2 * n + 64) * 2)) { printf("FAIL 1M\n"); ++fails; }
      sNCat(d1, s, 100000); wia_strncatw(d2, s, 100000);
      ++tested;
      if (memcmp(d1, d2, (2 * n + 64) * 2)) { printf("FAIL 1M truncated\n"); ++fails; }
      printf("  8. 1M characters:                    %lld fails\n", fails); }

    if (!fails) printf("CORRECTNESS: PASS (StrNCatW vs live shlwapi + oracle, whole arena, %lld cases: lengths x every n x odd offsets, overlap above and below the source, NOACCESS guards, the fault point and written prefix incl. src[n-1] unreadable, unterminated dst, NULLs, n<0 on read-only memory, 1M chars)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
