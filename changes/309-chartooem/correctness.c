/* changes/309-chartooem/correctness.c
 * Gate 1: the four converters against live user32 and the oracle -- return values and the whole
 * destination arena.
 *
 *   1. every UTF-16 unit through CharToOemBuffW, every byte through OemToCharBuffW
 *   2. random buffers 0..300 (units across the whole range, surrogates and pairs included; every byte
 *      value) at every source and destination offset parity, and random strings for the string forms
 *   3. the source ending at a NOACCESS page; a count or an unterminated source running into it: the
 *      Buff forms fault with the readable prefix written, the string forms fault with nothing written
 *   4. a destination that turns read-only part way: same prefix written before the fault
 *   5. a destination overlapping the source, every byte distance -60..+100, both directions
 *   6. NULL, src == dst, n == 0, and the counts handed to the export
 *   7. 1M units
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef BOOL (WINAPI *PC2OB)(LPCWSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *PC2O)(LPCWSTR, LPSTR);
typedef BOOL (WINAPI *PO2CB)(LPCSTR, LPWSTR, DWORD);
typedef BOOL (WINAPI *PO2C)(LPCSTR, LPWSTR);
extern BOOL wia_chartooembuffw(LPCWSTR, LPSTR, DWORD);
extern BOOL wia_chartooemw(LPCWSTR, LPSTR);
extern BOOL wia_oemtocharbuffw(LPCSTR, LPWSTR, DWORD);
extern BOOL wia_oemtocharw(LPCSTR, LPWSTR);
int wia_c2o_init(void);
BOOL ref_c2ob(LPCWSTR, LPSTR, DWORD); BOOL ref_c2o(LPCWSTR, LPSTR);
BOOL ref_o2cb(LPCSTR, LPWSTR, DWORD); BOOL ref_o2c(LPCSTR, LPWSTR);

static PC2OB sC2OB; static PC2O sC2O; static PO2CB sO2CB; static PO2C sO2C;
static long long tested, fails;
#define AR 2400
static unsigned char D1[AR], D2[AR], D3[AR];
static unsigned rng = 2718u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void report(const char* what, int n, BOOL a, BOOL b, BOOL c) {
    ++tested;
    if (a != b || a != c || memcmp(D1, D2, AR) || memcmp(D1, D3, AR)) {
        if (fails < 15) { int i = 0; while (i < AR && D1[i] == D2[i] && D1[i] == D3[i]) ++i;
            printf("FAIL %s n=%d ret %d %d %d first diff %d: sys %02X ours %02X ref %02X\n", what, n, a, b, c, i, D1[i], D2[i], D3[i]); }
        ++fails;
    }
}
static void reset(void) { memset(D1, 0xA5, AR); memset(D2, 0xA5, AR); memset(D3, 0xA5, AR); }

static int f_c2ob(int ours, LPCWSTR s, LPSTR d, DWORD n) { __try { if (ours) wia_chartooembuffw(s, d, n); else sC2OB(s, d, n); return 0; } __except (1) { return 1; } }
static int f_c2o(int ours, LPCWSTR s, LPSTR d) { __try { if (ours) wia_chartooemw(s, d); else sC2O(s, d); return 0; } __except (1) { return 1; } }
static int f_o2cb(int ours, LPCSTR s, LPWSTR d, DWORD n) { __try { if (ours) wia_oemtocharbuffw(s, d, n); else sO2CB(s, d, n); return 0; } __except (1) { return 1; } }
static int f_o2c(int ours, LPCSTR s, LPWSTR d) { __try { if (ours) wia_oemtocharw(s, d); else sO2C(s, d); return 0; } __except (1) { return 1; } }

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (!wia_c2o_init()) { printf("TABLES: init refused\n"); return 3; }
    HMODULE u = LoadLibraryW(L"user32.dll");
    sC2OB = (PC2OB)GetProcAddress(u, "CharToOemBuffW"); sC2O = (PC2O)GetProcAddress(u, "CharToOemW");
    sO2CB = (PO2CB)GetProcAddress(u, "OemToCharBuffW"); sO2C = (PO2C)GetProcAddress(u, "OemToCharW");

    /* 1 */
    for (unsigned c = 0; c < 65536; ++c) {
        wchar_t w = (wchar_t)c; reset();
        report("unit", 1, sC2OB(&w, (LPSTR)D1 + 64, 1), wia_chartooembuffw(&w, (LPSTR)D2 + 64, 1), ref_c2ob(&w, (LPSTR)D3 + 64, 1));
    }
    for (unsigned b = 0; b < 256; ++b) {
        char c = (char)b; reset();
        report("byte", 1, sO2CB(&c, (LPWSTR)(D1 + 64), 1), wia_oemtocharbuffw(&c, (LPWSTR)(D2 + 64), 1), ref_o2cb(&c, (LPWSTR)(D3 + 64), 1));
    }
    printf("  1. every unit and every byte:        %lld fails\n", fails);

    /* 2 */
    { long long f0 = fails;
      static wchar_t ws[400]; static unsigned char bs[400]; static unsigned char srcbuf[1200];
      for (int t = 0; t < 30000; ++t) {
          int n = (t < 3000) ? t % 301 : (int)(rnd() % 301);
          int so = rnd() % 34, doff = rnd() % 34;
          wchar_t* s = (wchar_t*)(srcbuf + 64 + so);
          for (int i = 0; i < n; ++i) { unsigned r = rnd(); s[i] = (wchar_t)((r & 3) == 0 ? r >> 4 : (r & 3) == 1 ? 1 + r % 0x7F : (r & 3) == 2 ? 0x80 + r % 0x500 : 0xD800 + r % 0x800); }
          s[n] = 0;
          reset();
          report("c2ob", n, sC2OB(s, (LPSTR)D1 + 64 + doff, n), wia_chartooembuffw(s, (LPSTR)D2 + 64 + doff, n), ref_c2ob(s, (LPSTR)D3 + 64 + doff, n));
          for (int i = 0; i < n; ++i) if (!s[i]) s[i] = 'x';
          reset();
          report("c2o", n, sC2O(s, (LPSTR)D1 + 64 + doff), wia_chartooemw(s, (LPSTR)D2 + 64 + doff), ref_c2o(s, (LPSTR)D3 + 64 + doff));
          char* b = (char*)(srcbuf + 64 + so);
          for (int i = 0; i < n; ++i) { unsigned r = rnd(); b[i] = (char)((r & 1) ? 0x20 + r % 0x5F : r >> 3); }
          b[n] = 0;
          reset();
          report("o2cb", n, sO2CB(b, (LPWSTR)(D1 + 64 + doff), n), wia_oemtocharbuffw(b, (LPWSTR)(D2 + 64 + doff), n), ref_o2cb(b, (LPWSTR)(D3 + 64 + doff), n));
          for (int i = 0; i < n; ++i) if (!b[i]) b[i] = 'y';
          reset();
          report("o2c", n, sO2C(b, (LPWSTR)(D1 + 64 + doff)), wia_oemtocharw(b, (LPWSTR)(D2 + 64 + doff)), ref_o2c(b, (LPWSTR)(D3 + 64 + doff)));
      }
      printf("  2. random buffers and strings:       %lld fails\n", fails - f0); }

    /* 3 */
    { long long f0 = fails;
      unsigned char* pg = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
      VirtualAlloc(pg, 0x2000, MEM_COMMIT, PAGE_READWRITE);
      static unsigned char a1[1200], a2[1200];
      for (int odd = 0; odd < 2; ++odd)
          for (int n = 1; n <= 200; ++n) {
              wchar_t* s = (wchar_t*)(pg + 0x2000 - n * 2 - odd);
              for (int i = 0; i < n; ++i) { ((unsigned char*)s)[2 * i] = (unsigned char)('a' + i % 26); ((unsigned char*)s)[2 * i + 1] = (unsigned char)((i % 7 == 3) ? 1 : 0); }
              if (odd) ((unsigned char*)s)[2 * n] = 'q';
              DWORD cnts[] = { (DWORD)n, (DWORD)n + 1, (DWORD)n + 500 };
              for (int k = 0; k < 3; ++k) {
                  memset(a1, 0x5A, sizeof a1); memset(a2, 0x5A, sizeof a2);
                  int f1 = f_c2ob(0, s, (LPSTR)a1 + 3, cnts[k]), f2 = f_c2ob(1, s, (LPSTR)a2 + 3, cnts[k]);
                  ++tested;
                  if (f1 != f2 || memcmp(a1, a2, sizeof a1)) { if (fails < 15) printf("FAIL c2ob guard n=%d cnt=%lu odd=%d fault %d %d\n", n, cnts[k], odd, f1, f2); ++fails; }
              }
              memset(a1, 0x5A, sizeof a1); memset(a2, 0x5A, sizeof a2);
              int f1 = f_c2o(0, s, (LPSTR)a1 + 3), f2 = f_c2o(1, s, (LPSTR)a2 + 3);
              ++tested;
              if (!f1 || !f2 || memcmp(a1, a2, sizeof a1)) { if (fails < 15) printf("FAIL c2o unterminated n=%d odd=%d fault %d %d\n", n, odd, f1, f2); ++fails; }
              char* b = (char*)(pg + 0x2000 - n);
              for (int i = 0; i < n; ++i) b[i] = (char)((i % 5 == 2) ? 0xB3 : 'a' + i % 26);
              DWORD cb[] = { (DWORD)n, (DWORD)n + 1, (DWORD)n + 500 };
              for (int k = 0; k < 3; ++k) {
                  memset(a1, 0x5A, sizeof a1); memset(a2, 0x5A, sizeof a2);
                  f1 = f_o2cb(0, b, (LPWSTR)(a1 + 4 + odd), cb[k]); f2 = f_o2cb(1, b, (LPWSTR)(a2 + 4 + odd), cb[k]);
                  ++tested;
                  if (f1 != f2 || memcmp(a1, a2, sizeof a1)) { if (fails < 15) printf("FAIL o2cb guard n=%d cnt=%lu fault %d %d\n", n, cb[k], f1, f2); ++fails; }
              }
              memset(a1, 0x5A, sizeof a1); memset(a2, 0x5A, sizeof a2);
              f1 = f_o2c(0, b, (LPWSTR)(a1 + 4)); f2 = f_o2c(1, b, (LPWSTR)(a2 + 4));
              ++tested;
              if (!f1 || !f2 || memcmp(a1, a2, sizeof a1)) { if (fails < 15) printf("FAIL o2c unterminated n=%d fault %d %d\n", n, f1, f2); ++fails; }
          }
      printf("  3. NOACCESS source, both kinds:      %lld fails\n", fails - f0); }

    /* 4 */
    { long long f0 = fails;
      static wchar_t ws[300]; static char bs[300];
      for (int i = 0; i < 300; ++i) { ws[i] = (wchar_t)((i % 9 == 4) ? 0xE9 : 'A' + i % 26); bs[i] = (char)((i % 9 == 4) ? 0xB0 : 'A' + i % 26); }
      for (int n = 2; n <= 260; n += 3)
          for (int fn = 0; fn < 2; ++fn) {
              unsigned char* pg[2];
              for (int side = 0; side < 2; ++side) {
                  pg[side] = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                  memset(pg[side], 0x77, 0x2000);
                  DWORD old; VirtualProtect(pg[side] + 0x1000, 0x1000, PAGE_READONLY, &old);
                  int f = fn == 0 ? f_c2ob(side, ws, (LPSTR)(pg[side] + 0x1000 - n / 2), (DWORD)n)
                                  : f_o2cb(side, bs, (LPWSTR)(pg[side] + 0x1000 - (n / 2) * 2 - (n & 1)), (DWORD)n);
                  if (!f) { printf("FAIL read-only dst: no fault\n"); ++fails; }
              }
              ++tested;
              if (memcmp(pg[0], pg[1], 0x2000)) { if (fails < 15) printf("FAIL fn=%d half read-only dst n=%d: different prefixes\n", fn, n); ++fails; }
              VirtualFree(pg[0], 0, MEM_RELEASE); VirtualFree(pg[1], 0, MEM_RELEASE);
          }
      printf("  4. destination read-only part way:   %lld fails\n", fails - f0); }

    /* 5 */
    { long long f0 = fails;
      static unsigned char B1[2000], B2[2000];
      for (int n = 1; n <= 120; n += (n < 40 ? 1 : 9))
          for (int delta = -60; delta <= 100; ++delta) {
              if (delta == 0) continue;
              for (int fn = 0; fn < 2; ++fn) {
                  for (int i = 0; i < 2000; ++i) B1[i] = B2[i] = (unsigned char)(i * 37 + 11);
                  if (fn == 0) {
                      wchar_t* s1 = (wchar_t*)(B1 + 600); wchar_t* s2 = (wchar_t*)(B2 + 600);
                      sC2OB(s1, (LPSTR)B1 + 600 + delta, (DWORD)n); wia_chartooembuffw(s2, (LPSTR)B2 + 600 + delta, (DWORD)n);
                  } else {
                      sO2CB((LPCSTR)B1 + 600, (LPWSTR)(B1 + 600 + delta), (DWORD)n); wia_oemtocharbuffw((LPCSTR)B2 + 600, (LPWSTR)(B2 + 600 + delta), (DWORD)n);
                  }
                  ++tested;
                  if (memcmp(B1, B2, 2000)) { if (fails < 15) printf("FAIL overlap fn=%d n=%d delta=%d\n", fn, n, delta); ++fails; }
              }
          }
      printf("  5. overlapping source and destination: %lld fails\n", fails - f0); }

    /* 6 */
    { long long f0 = fails;
      wchar_t w[4] = L"ab"; char c[8] = "zz"; char b2[4] = "ab"; wchar_t d[8];
      tested += 10;
      if (wia_chartooembuffw(NULL, c, 2) != sC2OB(NULL, c, 2)) ++fails;
      if (wia_chartooembuffw(w, NULL, 2) != sC2OB(w, NULL, 2)) ++fails;
      if (wia_chartooembuffw(w, (LPSTR)w, 2) != sC2OB(w, (LPSTR)w, 2)) ++fails;
      if (wia_chartooembuffw(w, c, 0) != sC2OB(w, c, 0) || strcmp(c, "zz")) ++fails;
      if (wia_oemtocharbuffw(b2, d, 0) != sO2CB(b2, d, 0)) ++fails;
      if (wia_oemtocharbuffw(NULL, d, 2) != sO2CB(NULL, d, 2)) ++fails;
      if (wia_chartooemw(NULL, c) != sC2O(NULL, c)) ++fails;
      if (wia_oemtocharw(b2, NULL) != sO2C(b2, NULL)) ++fails;
      if (wia_chartooemw(w, (LPSTR)w) != sC2O(w, (LPSTR)w)) ++fails;
      if (wia_oemtocharw((LPCSTR)d, d) != sO2C((LPCSTR)d, d)) ++fails;
      DWORD big[] = { 0x40000000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
      for (int k = 0; k < 4; ++k) {
          char x1[8], x2[8]; memset(x1, 0x55, 8); memset(x2, 0x55, 8);
          BOOL r1 = sC2OB(L"xyz", x1, big[k]), r2 = wia_chartooembuffw(L"xyz", x2, big[k]);
          ++tested; if (r1 != r2 || memcmp(x1, x2, 8)) { printf("FAIL c2ob count 0x%08lX\n", big[k]); ++fails; }
          if (big[k] >= 0x80000000) {
              wchar_t y1[8], y2[8]; for (int i = 0; i < 8; ++i) y1[i] = y2[i] = 0x5555;
              BOOL q1 = sO2CB("xyz", y1, big[k]), q2 = wia_oemtocharbuffw("xyz", y2, big[k]);
              ++tested; if (q1 != q2 || memcmp(y1, y2, 16)) { printf("FAIL o2cb count 0x%08lX\n", big[k]); ++fails; }
          }
      }
      printf("  6. NULL, same, n=0, handed-off:      %lld fails\n", fails - f0); }

    /* 7 */
    { size_t n = 1u << 20;
      wchar_t* s = (wchar_t*)malloc((n + 1) * 2); char* o1 = (char*)malloc(n + 1); char* o2 = (char*)malloc(n + 1);
      for (size_t i = 0; i < n; ++i) s[i] = (wchar_t)((i % 13 == 5) ? 0x400 + i % 64 : 'a' + i % 26);
      s[n] = 0;
      sC2OB(s, o1, (DWORD)n); wia_chartooembuffw(s, o2, (DWORD)n);
      ++tested; if (memcmp(o1, o2, n)) { printf("FAIL 1M c2ob\n"); ++fails; }
      wchar_t* w1 = (wchar_t*)malloc((n + 1) * 2); wchar_t* w2 = (wchar_t*)malloc((n + 1) * 2);
      for (size_t i = 0; i < n; ++i) o1[i] = (char)(0x20 + i % 0xDF); o1[n] = 0;
      sO2C(o1, w1); wia_oemtocharw(o1, w2);
      ++tested; if (memcmp(w1, w2, (n + 1) * 2)) { printf("FAIL 1M o2c\n"); ++fails; }
      printf("  7. 1M units:                         %lld fails\n", fails); }

    if (!fails) printf("CORRECTNESS: PASS (CharToOemBuffW/CharToOemW/OemToCharBuffW/OemToCharW vs live user32 + oracle, %lld cases: every unit and byte, random buffers and strings incl. surrogates, NOACCESS sources with the sequential prefix, read-only destinations, overlap -60..+100, NULLs, handed-off counts, 1M)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
