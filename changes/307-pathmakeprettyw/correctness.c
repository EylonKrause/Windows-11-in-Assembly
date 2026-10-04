/* changes/307-pathmakeprettyw/correctness.c
 * Gate 1: wia_pathmakeprettyw against live shlwapi!PathMakePrettyW and the oracle, comparing the
 * return value and the WHOLE poisoned arena around the path.
 *
 *   1. every code unit at index 1 (after 'A') and at index 0 (alone, and before "B")
 *   2. random paths of 0..300 units -- ASCII upper, digits, separators, Latin-1, Greek, Cyrillic,
 *      Deseret pairs and lone surrogates, now and then one 'a'..'z' -- at every offset parity, with
 *      lengths 256..262 over-represented (the 259 truncation)
 *   3. Deseret pairs placed across every boundary that matters: index 0, 257/258, 258/259, 259/260,
 *      and the 16-unit block edges
 *   4. the NUL as the last wchar before NOACCESS, every length and parity; an unterminated path into
 *      NOACCESS -- both fault, nothing written
 *   5. read-only memory: nothing to change still faults (every unit is written), a refusal does not,
 *      an empty path faults (unit 0 is written); a path that runs from a writable page into a
 *      read-only one -- both fault, with the same units written before the fault
 *   6. NULL
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PFN)(LPWSTR);
extern BOOL wia_pathmakeprettyw(LPWSTR);
BOOL ref_pathmakeprettyw(wchar_t*);
int wia_pmp_init(void);

static PFN sys;
static long long tested, fails;
#define AR 1600
static unsigned char A1[AR], A2[AR], A3[AR];
static unsigned rng = 4242u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void chk_buf(const wchar_t* src, int n, int off, const char* what) {
    memset(A1, 0xA5, AR);
    wchar_t* p1 = (wchar_t*)(A1 + 64 + off);
    memcpy(p1, src, n * 2); p1[n] = 0;
    memcpy(A2, A1, AR); memcpy(A3, A1, AR);
    wchar_t* p2 = (wchar_t*)(A2 + 64 + off); wchar_t* p3 = (wchar_t*)(A3 + 64 + off);
    BOOL r1 = sys(p1), r2 = wia_pathmakeprettyw(p2), r3 = ref_pathmakeprettyw(p3);
    ++tested;
    if (r1 != r2 || r1 != r3 || memcmp(A1, A2, AR) || memcmp(A1, A3, AR)) {
        if (fails < 15) {
            int i = 0; while (i < AR && A1[i] == A2[i] && A1[i] == A3[i]) ++i;
            printf("FAIL %s n=%d off=%d ret %d %d %d first diff byte %d (unit %d): sys %02X ours %02X ref %02X\n",
                   what, n, off, r1, r2, r3, i, (i - 64 - off) / 2, A1[i], A2[i], A3[i]);
        }
        ++fails;
    }
}

static wchar_t gen_unit(void) {
    unsigned r = rnd() % 100;
    if (r < 40) return (wchar_t)(L'A' + rnd() % 26);
    if (r < 50) return (wchar_t)(L'0' + rnd() % 10);
    if (r < 56) return L"\\:._- "[rnd() % 6];
    if (r < 66) return (wchar_t)(0xC0 + rnd() % 64);
    if (r < 72) return (wchar_t)(0x391 + rnd() % 40);
    if (r < 78) return (wchar_t)(0x400 + rnd() % 64);
    if (r < 84) return (wchar_t)(0x100 + rnd() % 0x180);
    if (r < 87) return (wchar_t)(0xD800 + rnd() % 0x800);        /* a lone surrogate */
    if (r < 89) return (wchar_t)(L'a' + rnd() % 26);             /* a refusal */
    return (wchar_t)(1 + rnd() % 0xFFFE);
}

static int faults(int ours, wchar_t* p) {
    __try { if (ours) wia_pathmakeprettyw(p); else sys(p); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (!wia_pmp_init()) { printf("TABLES: LCMapStringW no longer matches the rules impl.asm relies on\n"); return 3; }
    sys = (PFN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "PathMakePrettyW");
    if (!sys) { printf("missing export\n"); return 2; }

    /* 1 */
    for (int c = 1; c <= 0xFFFF; ++c) {
        wchar_t s1[2] = { L'A', (wchar_t)c }; chk_buf(s1, 2, c & 1 ? 2 : 0, "unit@1");
        wchar_t s0[1] = { (wchar_t)c };       chk_buf(s0, 1, 0, "unit@0");
        wchar_t s2[2] = { (wchar_t)c, L'B' }; chk_buf(s2, 2, 6, "unit@0+B");
    }
    printf("  1. every unit at index 0 and 1:      %lld fails\n", fails);

    /* 2 */
    { long long f0 = fails;
      static wchar_t s[400];
      for (int t = 0; t < 120000; ++t) {
          int n = (t & 3) == 0 ? 256 + rnd() % 7 : (int)(rnd() % 301);
          for (int i = 0; i < n; ++i) s[i] = gen_unit();
          if (rnd() % 3 == 0) for (int i = 0; i < n; ++i) if (s[i] >= L'a' && s[i] <= L'z') s[i] -= 32;   /* no refusal */
          if (rnd() % 4 == 0 && n >= 2) { int k = rnd() % (n - 1); s[k] = 0xD801; s[k + 1] = (wchar_t)(0xDC00 + rnd() % 0x50); }
          chk_buf(s, n, (rnd() % 34) & ~0, "random");
      }
      printf("  2. random paths 0..300 units:        %lld fails\n", fails - f0); }

    /* 3 */
    { long long f0 = fails;
      static wchar_t s[400];
      int pos[] = { 0, 1, 14, 15, 16, 30, 31, 32, 256, 257, 258, 259, 260 };
      for (int n = 2; n <= 300; ++n)
          for (int k = 0; k < (int)(sizeof pos / sizeof pos[0]); ++k) {
              if (pos[k] + 1 >= n) continue;
              for (int i = 0; i < n; ++i) s[i] = (wchar_t)(L'A' + i % 26);
              s[pos[k]] = 0xD801; s[pos[k] + 1] = (wchar_t)(0xDC00 + (n * 7 + k) % 0x28);
              chk_buf(s, n, (n & 1) * 2 + 1, "deseret");          /* odd byte offsets too */
              chk_buf(s, n, (n & 1) * 2, "deseret");
          }
      printf("  3. Deseret pairs at the boundaries:  %lld fails\n", fails - f0); }

    /* 4 */
    { long long f0 = fails;
      unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
      VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
      static wchar_t cpy1[400];
      for (int odd = 0; odd < 2; ++odd)
          for (int n = 0; n <= 300; ++n) {
              wchar_t* p = (wchar_t*)(b + 0x2000 - (n + 1) * 2 - odd);
              for (int i = 0; i < n; ++i) p[i] = (wchar_t)(L'A' + (i * 7) % 26);
              p[n] = 0;
              memcpy(cpy1, p, (n + 1) * 2);
              ref_pathmakeprettyw(cpy1);
              BOOL r = wia_pathmakeprettyw(p);             /* one byte of over-read and this faults */
              ++tested;
              if (r != 1 || memcmp(p, cpy1, (n + 1) * 2)) { if (fails < 15) printf("FAIL guard n=%d odd=%d\n", n, odd); ++fails; }
          }
      for (int odd = 0; odd < 2; ++odd)
          for (int n = 1; n <= 200; ++n) {
              wchar_t* p = (wchar_t*)(b + 0x2000 - n * 2 - odd);
              for (int side = 0; side < 2; ++side) {
                  for (int i = 0; i < n; ++i) { ((unsigned char*)p)[2 * i] = (unsigned char)('A' + i % 26); ((unsigned char*)p)[2 * i + 1] = 0; }
                  if (odd) ((unsigned char*)p)[2 * n] = 'Q';
                  int f = faults(side, p);
                  int same = 1; for (int i = 0; i < n; ++i) if (((unsigned char*)p)[2 * i] != (unsigned char)('A' + i % 26)) same = 0;
                  ++tested;
                  if (!f || !same) { if (fails < 15) printf("FAIL %s unterminated n=%d odd=%d fault=%d unchanged=%d\n", side ? "ours" : "sys", n, odd, f, same); ++fails; }
              }
          }
      printf("  4. NOACCESS guards, unterminated:    %lld fails\n", fails - f0); }

    /* 5 */
    { long long f0 = fails;
      const wchar_t* subj[] = { L"123\\456", L"abc", L"", L"C:\\WINDOWS", L"\x00C0\x00C9" };
      int expect[] = { 1, 0, 1, 1, 1 };
      for (int k = 0; k < 5; ++k)
          for (int side = 0; side < 2; ++side) {
              wchar_t* ro = (wchar_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
              wcscpy(ro, subj[k]);
              DWORD old; VirtualProtect(ro, 0x1000, PAGE_READONLY, &old);
              int f = faults(side, ro);
              ++tested;
              if (f != expect[k]) { printf("FAIL %s read-only \"%ls\": fault %d, expected %d\n", side ? "ours" : "sys", subj[k], f, expect[k]); ++fails; }
              VirtualFree(ro, 0, MEM_RELEASE);
          }
      /* writable page, then a read-only page: the same prefix written before the fault */
      for (int n = 2; n <= 140; ++n)
          for (int odd = 0; odd < 2; ++odd) {
              unsigned char* pg[2];
              for (int side = 0; side < 2; ++side) {
                  pg[side] = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                  wchar_t* p = (wchar_t*)(pg[side] + 0x1000 - (n / 2) * 2 - odd);
                  for (int i = 0; i < n; ++i) { ((unsigned char*)p)[2 * i] = (unsigned char)('A' + i % 26); ((unsigned char*)p)[2 * i + 1] = 0; }
                  ((unsigned char*)p)[2 * n] = 0; ((unsigned char*)p)[2 * n + 1] = 0;
                  DWORD old; VirtualProtect(pg[side] + 0x1000, 0x1000, PAGE_READONLY, &old);
                  int f = faults(side, p);
                  if (!f) { printf("FAIL %s half read-only n=%d: no fault\n", side ? "ours" : "sys", n); ++fails; }
              }
              ++tested;
              if (memcmp(pg[0], pg[1], 0x2000)) { if (fails < 15) printf("FAIL half read-only n=%d odd=%d: different units written before the fault\n", n, odd); ++fails; }
              VirtualFree(pg[0], 0, MEM_RELEASE); VirtualFree(pg[1], 0, MEM_RELEASE);
          }
      printf("  5. read-only and half read-only:     %lld fails\n", fails - f0); }

    /* 6 */
    { ++tested; if (wia_pathmakeprettyw(NULL) != sys(NULL)) { printf("FAIL NULL\n"); ++fails; } }

    if (!fails) printf("CORRECTNESS: PASS (PathMakePrettyW vs live shlwapi + oracle, whole arena, %lld cases: every unit at index 0 and 1, random paths to 300 units incl. Deseret pairs and lone surrogates, the 259 truncation, NOACCESS guards, unterminated, read-only and half read-only, NULL)\n", tested);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
