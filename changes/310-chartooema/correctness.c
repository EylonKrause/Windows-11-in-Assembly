/* changes/310-chartooema/correctness.c
 * Gate 1: the four converters against live user32 and the oracle -- return values and the whole
 * arena (source and destination share it, so in-place and overlapping calls are compared too).
 *
 *   1. every byte value, both directions, Buff and string forms
 *   2. random buffers 0..300 bytes, every byte value, at every offset; random strings
 *   3. in place, and every destination distance -70..+90 from the source, both forms
 *   4. a source running into NOACCESS -- count or unterminated string: both fault with the readable
 *      prefix written -- and the source ending exactly at the page
 *   5. a destination that turns read-only part way
 *   6. NULL, n == 0
 *   7. 1M bytes
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef BOOL (WINAPI *PB)(LPCSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *PS)(LPCSTR, LPSTR);
extern BOOL wia_chartooembuffa(LPCSTR, LPSTR, DWORD);
extern BOOL wia_chartooema(LPCSTR, LPSTR);
extern BOOL wia_oemtocharbuffa(LPCSTR, LPSTR, DWORD);
extern BOOL wia_oemtochara(LPCSTR, LPSTR);
int wia_c2oa_init(void);
extern int wia_c2oa_off, wia_o2ca_off;
BOOL ref_c2ob(LPCSTR, LPSTR, DWORD); BOOL ref_c2o(LPCSTR, LPSTR);
BOOL ref_o2cb(LPCSTR, LPSTR, DWORD); BOOL ref_o2c(LPCSTR, LPSTR);

static PB sB[2]; static PS sS[2];
static PB oB[2] = { wia_chartooembuffa, wia_oemtocharbuffa };
static PS oS[2] = { wia_chartooema, wia_oemtochara };
static PB rB[2] = { ref_c2ob, ref_o2cb };
static PS rS[2] = { ref_c2o, ref_o2c };
static const char* NM[2] = { "CharToOem", "OemToChar" };
static long long tested, fails;
#define AR 2400
static unsigned char A1[AR], A2[AR], A3[AR];
static unsigned rng = 1618u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

/* the subject is laid out in A*, the call reads at soff and writes at doff (both arena offsets) */
static void run(int fn, int str, int soff, int doff, DWORD n, const char* what) {
    memcpy(A2, A1, AR); memcpy(A3, A1, AR);
    BOOL r1 = str ? sS[fn]((LPCSTR)A1 + soff, (LPSTR)A1 + doff) : sB[fn]((LPCSTR)A1 + soff, (LPSTR)A1 + doff, n);
    BOOL r2 = str ? oS[fn]((LPCSTR)A2 + soff, (LPSTR)A2 + doff) : oB[fn]((LPCSTR)A2 + soff, (LPSTR)A2 + doff, n);
    BOOL r3 = str ? rS[fn]((LPCSTR)A3 + soff, (LPSTR)A3 + doff) : rB[fn]((LPCSTR)A3 + soff, (LPSTR)A3 + doff, n);
    ++tested;
    if (r1 != r2 || r1 != r3 || memcmp(A1, A2, AR) || memcmp(A1, A3, AR)) {
        if (fails < 15) { int i = 0; while (i < AR && A1[i] == A2[i] && A1[i] == A3[i]) ++i;
            printf("FAIL %s %s %s soff=%d doff=%d n=%lu ret %d %d %d first diff %d: sys %02X ours %02X ref %02X\n", NM[fn], str ? "string" : "buff", what, soff, doff, n, r1, r2, r3, i, A1[i], A2[i], A3[i]); }
        ++fails;
    }
}
static int faults(int ours, int fn, int str, LPCSTR s, LPSTR d, DWORD n) {
    __try { if (str) { if (ours) oS[fn](s, d); else sS[fn](s, d); } else { if (ours) oB[fn](s, d, n); else sB[fn](s, d, n); } return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (!wia_c2oa_init()) { printf("init failed\n"); return 3; }
    printf("fast paths: CharToOem %s, OemToChar %s\n", wia_c2oa_off ? "OFF (handed off)" : "on", wia_o2ca_off ? "OFF (handed off)" : "on");
    HMODULE u = LoadLibraryW(L"user32.dll");
    sB[0] = (PB)GetProcAddress(u, "CharToOemBuffA"); sS[0] = (PS)GetProcAddress(u, "CharToOemA");
    sB[1] = (PB)GetProcAddress(u, "OemToCharBuffA"); sS[1] = (PS)GetProcAddress(u, "OemToCharA");

    /* 1 */
    for (int fn = 0; fn < 2; ++fn)
        for (int b = 0; b < 256; ++b) {
            memset(A1, 0xA5, AR); A1[100] = (unsigned char)b; A1[101] = 0;
            run(fn, 0, 100, 600, 1, "byte"); run(fn, 1, 100, 600, 0, "byte");
        }
    printf("  1. every byte value:                 %lld fails\n", fails);

    /* 2 */
    { long long f0 = fails;
      for (int t = 0; t < 40000; ++t) {
          int fn = t & 1, n = (t < 4000) ? (t / 2) % 301 : (int)(rnd() % 301);
          int so = 64 + rnd() % 40, dof = 900 + rnd() % 40;
          memset(A1, 0xA5, AR);
          for (int i = 0; i < n; ++i) { unsigned r = rnd(); A1[so + i] = (unsigned char)((r & 3) ? 0x20 + r % 0x5F : r >> 4); }
          A1[so + n] = 0;
          run(fn, 0, so, dof, (DWORD)n, "random");
          for (int i = 0; i < n; ++i) if (!A1[so + i]) A1[so + i] = 'x';
          run(fn, 1, so, dof, 0, "random");
      }
      printf("  2. random buffers and strings:       %lld fails\n", fails - f0); }

    /* 3 */
    { long long f0 = fails;
      for (int n = 1; n <= 140; n += (n < 40 ? 1 : 11))
          for (int delta = -70; delta <= 90; ++delta)
              for (int fn = 0; fn < 2; ++fn) {
                  memset(A1, 0xA5, AR);
                  for (int i = 0; i < n; ++i) A1[1000 + i] = (unsigned char)((i % 3 == 1) ? 0x80 + (i * 7) % 0x7F : 0x20 + (i * 5) % 0x5F);
                  A1[1000 + n] = 0;
                  run(fn, 0, 1000, 1000 + delta, (DWORD)n, "overlap");
                  /* a string form with the destination 1..n bytes above the source overwrites the
                     source's own NUL before reading it: the export then never stops and writes through
                     the process until it faults. Not a comparison anyone can run safely. */
                  if (delta <= 0 || delta > n) run(fn, 1, 1000, 1000 + delta, 0, "overlap");
              }
      printf("  3. in place and overlapping:         %lld fails\n", fails - f0); }

    /* 4 */
    { long long f0 = fails;
      unsigned char* pg = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
      VirtualAlloc(pg, 0x2000, MEM_COMMIT, PAGE_READWRITE);
      static unsigned char d1[800], d2[800];
      for (int fn = 0; fn < 2; ++fn)
          for (int n = 1; n <= 200; ++n) {
              char* s = (char*)pg + 0x2000 - n;
              for (int i = 0; i < n; ++i) s[i] = (char)((i % 4 == 2) ? 0xC9 : 'a' + i % 26);
              DWORD cnt[] = { (DWORD)n, (DWORD)n + 1, (DWORD)n + 700 };
              for (int k = 0; k < 3; ++k) {
                  memset(d1, 0x55, sizeof d1); memset(d2, 0x55, sizeof d2);
                  int f1 = faults(0, fn, 0, s, (LPSTR)d1 + 3, cnt[k]), f2 = faults(1, fn, 0, s, (LPSTR)d2 + 3, cnt[k]);
                  ++tested;
                  if (f1 != f2 || f1 != (k > 0) || memcmp(d1, d2, sizeof d1)) { if (fails < 15) printf("FAIL %s buff guard n=%d cnt=%lu fault %d %d\n", NM[fn], n, cnt[k], f1, f2); ++fails; }
              }
              memset(d1, 0x55, sizeof d1); memset(d2, 0x55, sizeof d2);
              int f1 = faults(0, fn, 1, s, (LPSTR)d1 + 3, 0), f2 = faults(1, fn, 1, s, (LPSTR)d2 + 3, 0);
              ++tested;
              if (!f1 || !f2 || memcmp(d1, d2, sizeof d1)) { if (fails < 15) printf("FAIL %s string unterminated n=%d fault %d %d\n", NM[fn], n, f1, f2); ++fails; }
              s[n - 1] = 0;          /* terminated exactly at the page end */
              memset(d1, 0x55, sizeof d1); memset(d2, 0x55, sizeof d2);
              f1 = faults(0, fn, 1, s, (LPSTR)d1 + 3, 0); f2 = faults(1, fn, 1, s, (LPSTR)d2 + 3, 0);
              ++tested;
              if (f1 || f2 || memcmp(d1, d2, sizeof d1)) { if (fails < 15) printf("FAIL %s string at the page n=%d fault %d %d\n", NM[fn], n, f1, f2); ++fails; }
          }
      printf("  4. NOACCESS source:                  %lld fails\n", fails - f0); }

    /* 5 */
    { long long f0 = fails;
      static char src[400]; for (int i = 0; i < 400; ++i) src[i] = (char)((i % 5 == 1) ? 0xE9 : 'A' + i % 26);
      src[399] = 0;
      for (int fn = 0; fn < 2; ++fn)
          for (int n = 2; n <= 300; n += 7)
              for (int str = 0; str < 2; ++str) {
                  unsigned char* pg[2];
                  char s2[400]; memcpy(s2, src, 400); if (str) s2[n] = 0;
                  for (int side = 0; side < 2; ++side) {
                      pg[side] = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                      memset(pg[side], 0x66, 0x2000);
                      DWORD old; VirtualProtect(pg[side] + 0x1000, 0x1000, PAGE_READONLY, &old);
                      int f = faults(side, fn, str, s2, (LPSTR)pg[side] + 0x1000 - n / 2, (DWORD)n);
                      if (!f) { printf("FAIL read-only dst: no fault\n"); ++fails; }
                  }
                  ++tested;
                  if (memcmp(pg[0], pg[1], 0x2000)) { if (fails < 15) printf("FAIL %s half read-only n=%d str=%d\n", NM[fn], n, str); ++fails; }
                  VirtualFree(pg[0], 0, MEM_RELEASE); VirtualFree(pg[1], 0, MEM_RELEASE);
              }
      printf("  5. destination read-only part way:   %lld fails\n", fails - f0); }

    /* 6 */
    { long long f0 = fails; char d[8] = "zz";
      for (int fn = 0; fn < 2; ++fn) {
          tested += 5;
          if (oB[fn](NULL, d, 2) != sB[fn](NULL, d, 2)) ++fails;
          if (oB[fn]("ab", NULL, 2) != sB[fn]("ab", NULL, 2)) ++fails;
          if (oB[fn]("ab", d, 0) != sB[fn]("ab", d, 0) || strcmp(d, "zz")) ++fails;
          if (oS[fn](NULL, d) != sS[fn](NULL, d)) ++fails;
          if (oS[fn]("ab", NULL) != sS[fn]("ab", NULL)) ++fails;
      }
      printf("  6. NULL, n = 0:                      %lld fails\n", fails - f0); }

    /* 7 */
    { size_t n = 1u << 20;
      char* s = (char*)malloc(n + 1); char* d1 = (char*)malloc(n + 1); char* d2 = (char*)malloc(n + 1);
      for (size_t i = 0; i < n; ++i) s[i] = (char)(1 + rnd() % 255);
      s[n] = 0;
      for (int fn = 0; fn < 2; ++fn) {
          sB[fn](s, d1, (DWORD)n); oB[fn](s, d2, (DWORD)n);
          ++tested; if (memcmp(d1, d2, n)) { printf("FAIL 1M buff\n"); ++fails; }
          sS[fn](s, d1); oS[fn](s, d2);
          ++tested; if (memcmp(d1, d2, n + 1)) { printf("FAIL 1M string\n"); ++fails; }
      }
      printf("  7. 1M bytes:                         %lld fails\n", fails); }

    if (!fails) printf("CORRECTNESS: PASS (CharToOemBuffA/CharToOemA/OemToCharBuffA/OemToCharA vs live user32 + oracle, %lld cases: every byte value, random buffers and strings, in place and every overlap -70..+90, NOACCESS sources with the sequential prefix, read-only destinations, NULLs, 1M)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
