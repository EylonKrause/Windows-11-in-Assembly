/* changes/311-pathisurlw/correctness.c
 * Gate 1: wia_pathisurlw / wia_pathisurla against live shlwapi!PathIsURLW / PathIsURLA and the oracle.
 *
 *   1. every unit (W) / byte (A) at indices 0..5 and 20, before "x:" and before nothing
 *   2. random subjects of 0..300 units: scheme characters with a ':' somewhere, an invalid unit now and
 *      then, at every byte offset (odd ones for W)
 *   3. NOACCESS: a scheme run terminated just before it; unterminated into it (both fault); ':' as the
 *      last unit before it (no fault: nothing after ':' is read)
 *   4. NULL
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PW)(LPCWSTR);
typedef BOOL (WINAPI *PA)(LPCSTR);
extern BOOL wia_pathisurlw(LPCWSTR);
extern BOOL wia_pathisurla(LPCSTR);
int wia_piu_init(void);
extern int wia_piu_off;
BOOL ref_pathisurlw(LPCWSTR);
BOOL ref_pathisurla(LPCSTR);

static PW sW; static PA sA;
static long long tested, fails;
static unsigned rng = 404u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void cw(const wchar_t* s, const char* what) {
    BOOL a = sW(s), b = wia_pathisurlw(s), c = ref_pathisurlw(s);
    ++tested;
    if (a != b || a != c) { if (fails < 15) printf("FAIL W %s: sys %d ours %d ref %d (len %zu)\n", what, a, b, c, wcslen(s)); ++fails; }
}
static void ca(const char* s, const char* what) {
    BOOL a = sA(s), b = wia_pathisurla(s), c = ref_pathisurla(s);
    ++tested;
    if (a != b || a != c) { if (fails < 15) printf("FAIL A %s: sys %d ours %d ref %d (len %zu)\n", what, a, b, c, strlen(s)); ++fails; }
}
static int fw(int ours, LPCWSTR s) { __try { if (ours) wia_pathisurlw(s); else sW(s); return 0; } __except (1) { return 1; } }
static int fa(int ours, LPCSTR s) { __try { if (ours) wia_pathisurla(s); else sA(s); return 0; } __except (1) { return 1; } }

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (!wia_piu_init()) { printf("init failed\n"); return 3; }
    if (wia_piu_off) printf("note: the exports disagree with the set -- every call is handed to them\n");
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sW = (PW)GetProcAddress(h, "PathIsURLW"); sA = (PA)GetProcAddress(h, "PathIsURLA");

    /* 1 */
    { int idx[] = { 0, 1, 2, 3, 4, 5, 20 };
      static wchar_t w[64]; static char a[64];
      for (int k = 0; k < 7; ++k)
          for (int c = 1; c < 0x10000; ++c) {
              for (int i = 0; i < idx[k]; ++i) w[i] = (wchar_t)('a' + i % 26);
              w[idx[k]] = (wchar_t)c; w[idx[k] + 1] = 'x'; w[idx[k] + 2] = ':'; w[idx[k] + 3] = 0;
              cw(w, "unit");
              w[idx[k] + 1] = 0; cw(w, "unit-end");
              if (c < 256) {
                  for (int i = 0; i < idx[k]; ++i) a[i] = (char)('a' + i % 26);
                  a[idx[k]] = (char)c; a[idx[k] + 1] = 'x'; a[idx[k] + 2] = ':'; a[idx[k] + 3] = 0;
                  ca(a, "byte");
                  a[idx[k] + 1] = 0; ca(a, "byte-end");
              }
          }
      printf("  1. every unit at seven indices:      %lld fails\n", fails); }

    /* 2 */
    { long long f0 = fails;
      static const char set[] = "+-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
      static unsigned char wb[800], ab[400];
      for (int t = 0; t < 200000; ++t) {
          int n = (t & 1) ? (int)(rnd() % 40) : (int)(rnd() % 300);
          int off = rnd() % 34;
          wchar_t* w = (wchar_t*)(wb + 64 + off);
          char* a = (char*)ab + 32 + off;
          for (int i = 0; i < n; ++i) { unsigned r = rnd(); w[i] = (wchar_t)set[r % 65]; a[i] = (char)w[i]; }
          if (n && (rnd() & 1)) { int k = rnd() % n; w[k] = ':'; a[k] = ':'; }
          if (n && (rnd() % 4 == 0)) { int k = rnd() % n; unsigned r = rnd(); w[k] = (wchar_t)(r % 3 == 0 ? 0x100 + r % 0x500 : 1 + r % 0x7F); a[k] = (char)(r % 3 == 0 ? 0x80 + r % 0x7F : 1 + r % 0x7F); }
          w[n] = 0; a[n] = 0;
          cw(w, "random"); ca(a, "random");
      }
      printf("  2. random subjects:                  %lld fails\n", fails - f0); }

    /* 3 */
    { long long f0 = fails;
      unsigned char* pg = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
      VirtualAlloc(pg, 0x2000, MEM_COMMIT, PAGE_READWRITE);
      for (int odd = 0; odd < 2; ++odd)
          for (int n = 1; n <= 200; ++n) {
              wchar_t* w = (wchar_t*)(pg + 0x2000 - (n + 1) * 2 - odd);
              for (int i = 0; i < n; ++i) w[i] = (wchar_t)('a' + i % 26);
              w[n] = 0; cw(w, "guard");
              w[n - 1] = ':'; cw(w, "guard-colon");
              /* ':' as the very last unit before NOACCESS: nothing after it is read */
              wchar_t* c = (wchar_t*)(pg + 0x2000 - n * 2 - odd);
              for (int i = 0; i < n; ++i) c[i] = (wchar_t)('a' + i % 26);
              c[n - 1] = ':';
              if (odd) ((unsigned char*)c)[2 * n] = 0;
              int f1 = fw(0, c), f2 = fw(1, c);
              ++tested;
              if (f1 || f2 || sW(c) != wia_pathisurlw(c)) { if (fails < 15) printf("FAIL W colon at the page n=%d odd=%d fault %d %d\n", n, odd, f1, f2); ++fails; }
              /* unterminated */
              for (int i = 0; i < n; ++i) c[i] = (wchar_t)('a' + i % 26);
              f1 = fw(0, c); f2 = fw(1, c);
              ++tested;
              if (!f1 || !f2) { if (fails < 15) printf("FAIL W unterminated n=%d odd=%d fault %d %d\n", n, odd, f1, f2); ++fails; }
              char* a = (char*)pg + 0x2000 - n - 1;
              for (int i = 0; i < n; ++i) a[i] = (char)('a' + i % 26);
              a[n] = 0; ca(a, "guard");
              char* b = (char*)pg + 0x2000 - n;
              for (int i = 0; i < n; ++i) b[i] = (char)('a' + i % 26);
              b[n - 1] = ':';
              f1 = fa(0, b); f2 = fa(1, b);
              ++tested;
              if (f1 || f2 || sA(b) != wia_pathisurla(b)) { if (fails < 15) printf("FAIL A colon at the page n=%d fault %d %d\n", n, f1, f2); ++fails; }
              b[n - 1] = 'z';
              f1 = fa(0, b); f2 = fa(1, b);
              ++tested;
              if (!f1 || !f2) { if (fails < 15) printf("FAIL A unterminated n=%d fault %d %d\n", n, f1, f2); ++fails; }
          }
      printf("  3. NOACCESS guards and faults:       %lld fails\n", fails - f0); }

    /* 4 */
    { tested += 2; if (wia_pathisurlw(NULL) != sW(NULL)) ++fails; if (wia_pathisurla(NULL) != sA(NULL)) ++fails; }

    if (!fails) printf("CORRECTNESS: PASS (PathIsURLW/PathIsURLA vs live shlwapi + oracle, %lld cases: every unit and byte at seven indices, 400,000 random subjects at every offset, NOACCESS guards, ':' as the last readable unit, unterminated runs, NULL)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
