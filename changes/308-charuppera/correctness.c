/* changes/308-charuppera/correctness.c
 * Gate 1: the four functions against live user32!CharUpperA / CharLowerA / CharUpperBuffA /
 * CharLowerBuffA and the oracle, comparing return values and the WHOLE poisoned arena.
 *
 *   1. character mode, every value 1..0xFFFF, both directions
 *   2. Buff forms: buffers of 0..300 arbitrary bytes, embedded NULs included, at offsets 0..33
 *   3. string forms: strings of 0..300 non-NUL bytes at offsets 0..33
 *   4. NOACCESS: the terminator as the last byte before it; an unterminated string; a Buff count
 *      reaching into it -- all must fault with NOTHING written, or not fault
 *   5. read-only memory (every byte is written, so both fault even with nothing to change); a range
 *      that runs from a writable page into a read-only one -- same prefix written before the fault
 *   6. NULL, cch == 0, and the counts the dispatch hands to the export (0xFFFFFFFF, 0xFFFFFFFE)
 *   7. 1M bytes
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

typedef DWORD (WINAPI *PBUFF)(LPSTR, DWORD);
typedef LPSTR (WINAPI *PSTR_)(LPSTR);
extern DWORD wia_charupperbuffa(LPSTR, DWORD);
extern DWORD wia_charlowerbuffa(LPSTR, DWORD);
extern LPSTR wia_charuppera(LPSTR);
extern LPSTR wia_charlowera(LPSTR);
int wia_cua_init(void);
extern int wia_cua_dbcs;
DWORD ref_casebuffa(char*, DWORD, int);
char* ref_casea(char*, int);

static PBUFF sB[2]; static PSTR_ sS[2];
static PBUFF oB[2] = { wia_charlowerbuffa, wia_charupperbuffa };
static PSTR_ oS[2] = { wia_charlowera, wia_charuppera };
static const char* NM[2] = { "lower", "upper" };
static long long tested, fails;
#define AR 1200
static unsigned char A1[AR], A2[AR], A3[AR];
static unsigned rng = 31337u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void chk_buff(int up, const unsigned char* src, int n, int off, DWORD cch) {
    memset(A1, 0xA5, AR);
    memcpy(A1 + 64 + off, src, n);
    memcpy(A2, A1, AR); memcpy(A3, A1, AR);
    DWORD r1 = sB[up]((LPSTR)(A1 + 64 + off), cch), r2 = oB[up]((LPSTR)(A2 + 64 + off), cch), r3 = ref_casebuffa((char*)(A3 + 64 + off), cch, up);
    ++tested;
    if (r1 != r2 || r1 != r3 || memcmp(A1, A2, AR) || memcmp(A1, A3, AR)) {
        if (fails < 15) { int i = 0; while (i < AR && A1[i] == A2[i] && A1[i] == A3[i]) ++i;
            printf("FAIL %s buff n=%d off=%d ret %lu %lu %lu first diff %d: sys %02X ours %02X ref %02X\n", NM[up], n, off, r1, r2, r3, i, A1[i], A2[i], A3[i]); }
        ++fails;
    }
}
static void chk_str(int up, const unsigned char* src, int n, int off) {
    memset(A1, 0xA5, AR);
    memcpy(A1 + 64 + off, src, n); A1[64 + off + n] = 0;
    memcpy(A2, A1, AR); memcpy(A3, A1, AR);
    char* p1 = (char*)(A1 + 64 + off); char* p2 = (char*)(A2 + 64 + off); char* p3 = (char*)(A3 + 64 + off);
    LPSTR r1 = sS[up](p1), r2 = oS[up](p2); char* r3 = ref_casea(p3, up);
    ++tested;
    if (r1 != p1 || r2 != p2 || r3 != p3 || memcmp(A1, A2, AR) || memcmp(A1, A3, AR)) {
        if (fails < 15) { int i = 0; while (i < AR && A1[i] == A2[i] && A1[i] == A3[i]) ++i;
            printf("FAIL %s string n=%d off=%d first diff %d: sys %02X ours %02X ref %02X\n", NM[up], n, off, i, A1[i], A2[i], A3[i]); }
        ++fails;
    }
}
static int faults_b(int ours, int up, char* p, DWORD cch) { __try { if (ours) oB[up](p, cch); else sB[up](p, cch); return 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; } }
static int faults_s(int ours, int up, char* p) { __try { if (ours) oS[up](p); else sS[up](p); return 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; } }

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (!wia_cua_init()) { printf("TABLES: init refused\n"); return 3; }
    if (wia_cua_dbcs) printf("note: DBCS code page -- every call goes to the export\n");
    HMODULE u = LoadLibraryW(L"user32.dll");
    sB[0] = (PBUFF)GetProcAddress(u, "CharLowerBuffA"); sB[1] = (PBUFF)GetProcAddress(u, "CharUpperBuffA");
    sS[0] = (PSTR_)GetProcAddress(u, "CharLowerA");     sS[1] = (PSTR_)GetProcAddress(u, "CharUpperA");

    /* 1 */
    for (int up = 0; up < 2; ++up)
        for (uintptr_t v = 1; v < 0x10000; ++v) {
            LPSTR a = sS[up]((LPSTR)v), b = oS[up]((LPSTR)v); char* c = ref_casea((char*)v, up);
            ++tested;
            if (a != b || a != c) { if (fails < 15) printf("FAIL %s char 0x%04llX: sys %p ours %p ref %p\n", NM[up], (unsigned long long)v, (void*)a, (void*)b, (void*)c); ++fails; }
        }
    printf("  1. character mode, every value:      %lld fails\n", fails);

    /* 2, 3 */
    { long long f0 = fails;
      static unsigned char s[400];
      for (int up = 0; up < 2; ++up)
          for (int n = 0; n <= 300; n += (n < 80 ? 1 : 7))
              for (int off = 0; off < 34; ++off) {
                  for (int i = 0; i < n; ++i) { unsigned r = rnd(); s[i] = (unsigned char)((r & 3) ? 'A' + r % 58 : r >> 4); }
                  chk_buff(up, s, n, off, (DWORD)n);
                  for (int i = 0; i < n; ++i) if (!s[i]) s[i] = 'x';
                  chk_str(up, s, n, off);
              }
      for (int t = 0; t < 40000; ++t) {
          int n = rnd() % 300, up = t & 1;
          for (int i = 0; i < n; ++i) s[i] = (unsigned char)(rnd() & 0xFF);
          chk_buff(up, s, n, rnd() % 34, (DWORD)n);
          for (int i = 0; i < n; ++i) if (!s[i]) s[i] = 0xC9;
          chk_str(up, s, n, rnd() % 34);
      }
      printf("  2+3. Buff and string forms:          %lld fails\n", fails - f0); }

    /* 4 */
    { long long f0 = fails;
      unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
      VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
      static char e1[400];
      for (int up = 0; up < 2; ++up)
          for (int n = 0; n <= 300; ++n) {
              char* p = (char*)b + 0x2000 - n - 1;
              for (int i = 0; i < n; ++i) p[i] = (char)('a' + (i * 5) % 26);
              p[n] = 0;
              memcpy(e1, p, n + 1); ref_casea(e1, up);
              LPSTR r = oS[up](p);
              ++tested;
              if (r != p || memcmp(p, e1, n + 1)) { if (fails < 15) printf("FAIL %s guard n=%d\n", NM[up], n); ++fails; }
              for (int i = 0; i < n; ++i) p[i] = (char)('a' + (i * 5) % 26);
              DWORD rb = oB[up](p, (DWORD)(n + 1));
              memcpy(e1, p, n + 1);
              ++tested;
              if (rb != (DWORD)(n + 1)) { if (fails < 15) printf("FAIL %s guard buff n=%d\n", NM[up], n); ++fails; }
          }
      for (int up = 0; up < 2; ++up)
          for (int n = 1; n <= 200; ++n)
              for (int side = 0; side < 2; ++side) {
                  char* p = (char*)b + 0x2000 - n;
                  for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 26);
                  int f = faults_s(side, up, p);
                  int same = 1; for (int i = 0; i < n; ++i) if (p[i] != (char)('a' + i % 26)) same = 0;
                  ++tested;
                  if (!f || !same) { if (fails < 15) printf("FAIL %s %s unterminated n=%d fault=%d unchanged=%d\n", side ? "ours" : "sys", NM[up], n, f, same); ++fails; }
                  for (int extra = 1; extra <= 5000; extra *= 7) {
                      for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 26);
                      f = faults_b(side, up, p, (DWORD)(n + extra));
                      same = 1; for (int i = 0; i < n; ++i) if (p[i] != (char)('a' + i % 26)) same = 0;
                      ++tested;
                      if (!f || !same) { if (fails < 15) printf("FAIL %s %s buff into NOACCESS n=%d +%d fault=%d unchanged=%d\n", side ? "ours" : "sys", NM[up], n, extra, f, same); ++fails; }
                  }
              }
      printf("  4. NOACCESS guards and faults:       %lld fails\n", fails - f0); }

    /* 5 */
    { long long f0 = fails;
      for (int up = 0; up < 2; ++up)
          for (int side = 0; side < 2; ++side) {
              char* ro = (char*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
              strcpy(ro, "ABC123xyz");
              DWORD old; VirtualProtect(ro, 0x1000, PAGE_READONLY, &old);
              int f1 = faults_s(side, up, ro), f2 = faults_b(side, up, ro, 9), f3 = faults_b(side, up, ro, 0);
              tested += 3;
              if (!f1 || !f2 || f3) { printf("FAIL %s %s read-only: string %d buff %d buff0 %d\n", side ? "ours" : "sys", NM[up], f1, f2, f3); ++fails; }
              VirtualFree(ro, 0, MEM_RELEASE);
          }
      for (int up = 0; up < 2; ++up)
          for (int n = 2; n <= 200; n += 3)
              for (int mode = 0; mode < 2; ++mode) {
                  unsigned char* pg[2];
                  for (int side = 0; side < 2; ++side) {
                      pg[side] = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                      char* p = (char*)pg[side] + 0x1000 - n / 2;
                      for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 26);
                      p[n] = 0;
                      DWORD old; VirtualProtect(pg[side] + 0x1000, 0x1000, PAGE_READONLY, &old);
                      int f = mode ? faults_b(side, up, p, (DWORD)n) : faults_s(side, up, p);
                      if (!f) { printf("FAIL half read-only: no fault\n"); ++fails; }
                  }
                  ++tested;
                  if (memcmp(pg[0], pg[1], 0x2000)) { if (fails < 15) printf("FAIL %s half read-only n=%d mode=%d: different prefixes\n", NM[up], n, mode); ++fails; }
                  VirtualFree(pg[0], 0, MEM_RELEASE); VirtualFree(pg[1], 0, MEM_RELEASE);
              }
      printf("  5. read-only and half read-only:     %lld fails\n", fails - f0); }

    /* 6 */
    { long long f0 = fails;
      for (int up = 0; up < 2; ++up) {
          tested += 5;
          if (oS[up](NULL) != sS[up](NULL)) ++fails;
          char z[4] = "ab"; if (oB[up](z, 0) != 0 || strcmp(z, "ab")) ++fails;
          if (faults_b(1, up, NULL, 5) != faults_b(0, up, NULL, 5)) ++fails;
          DWORD cc[] = { 0xFFFFFFFF, 0xFFFFFFFE };
          for (int k = 0; k < 2; ++k) {
              char a1[16] = "abc\0def", a2[16] = "abc\0def";
              DWORD r1 = sB[up](a1, cc[k]), r2 = oB[up](a2, cc[k]);
              if (r1 != r2 || memcmp(a1, a2, 16)) { printf("FAIL %s count 0x%08lX\n", NM[up], cc[k]); ++fails; }
          }
      }
      printf("  6. NULL, cch 0, handed-off counts:   %lld fails\n", fails - f0); }

    /* 7 */
    { size_t n = 1u << 20;
      unsigned char* s1 = (unsigned char*)malloc(n + 1); unsigned char* s2 = (unsigned char*)malloc(n + 1);
      for (size_t i = 0; i < n; ++i) s1[i] = (unsigned char)(1 + rnd() % 255);
      s1[n] = 0; memcpy(s2, s1, n + 1);
      for (int up = 0; up < 2; ++up) {
          DWORD r1 = sB[up]((LPSTR)s1, (DWORD)n), r2 = oB[up]((LPSTR)s2, (DWORD)n);
          ++tested; if (r1 != r2 || memcmp(s1, s2, n + 1)) { printf("FAIL 1M buff\n"); ++fails; }
          sS[up]((LPSTR)s1); oS[up]((LPSTR)s2);
          ++tested; if (memcmp(s1, s2, n + 1)) { printf("FAIL 1M string\n"); ++fails; }
      }
      printf("  7. 1M bytes:                         %lld fails\n", fails); }

    if (!fails) printf("CORRECTNESS: PASS (CharUpperA/CharLowerA/CharUpperBuffA/CharLowerBuffA vs live user32 + oracle, whole arena, %lld cases: char mode every value, Buff with embedded NULs and every byte value, strings at every offset, NOACCESS guards and fault-before-write, read-only and half read-only, NULL, handed-off counts, 1M)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
