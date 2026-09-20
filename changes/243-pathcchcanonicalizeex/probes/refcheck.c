/* changes/243-pathcchcanonicalizeex/probes/refcheck.c
   Validate reference.c -- the ORACLE -- against the live export before any assembly exists.

   correctness.c will compare three ways (ours, the oracle, live). That only means something if the
   oracle itself is right first, so this file validates the oracle alone, over the whole space the
   contract has: the enumerated path alphabets, all 128 flag values, a cch sweep across the exact
   boundary, the two length caps, and flag bits above the documented seven.

   THE COMPARISON IS OF THE STRING, ITS TERMINATOR, AND WHETHER ANYTHING WAS WRITTEN AT ALL. Every
   case poison-fills the destination first, because several rules here are about what is NOT written:
   cch == 0 leaves the buffer untouched while cch == 1 empties it, and every error path empties it. A
   comparison that only looked at the returned string would pass all of those blind.

   What is deliberately NOT compared is the buffer BEYOND the terminator. The shipped implementation
   walks the path directly in the caller's buffer and truncates as it pops, so it leaves its own scratch
   behind -- "C:\a\.." comes back as "C:\" followed by the leftover "\" of the "C:\a\" it built on the
   way. That debris is an artefact of writing one character at a time, not a contract: reproducing it
   byte for byte would forbid ANY vectorised store, since a 32-byte store necessarily writes cells a
   scalar loop would not. correctness.c additionally guards the cells past cch with a canary, so an
   implementation that writes outside the buffer it was given still fails.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
static CANEX canex;
extern long wia_ref_pathcchcanonicalizeex(wchar_t*, size_t, const wchar_t*, unsigned long);

#define POISON 0xCDCD
#define BUFCCH 0x8200

static wchar_t liveb[BUFCCH], refb[BUFCCH];
static long long cases, bad, delegated;
static int shown;
static long long bad_by_flag[256], case_by_flag[256], bad_high, case_high;

static void fill(wchar_t* b, size_t n){ for (size_t i = 0; i < n; ++i) b[i] = POISON; }

static void show(const wchar_t* in, size_t cch, unsigned long flags,
                 long hl, long hr, size_t upto)
{
    /* flags 0 is the domain that must be exact, so it gets its own reporting budget */
    static int shown_zero;
    if (flags == 0) { if (shown_zero >= 30) return; ++shown_zero; }
    else            { if (shown >= 20) return;      ++shown; }
    printf("    MISMATCH in \"%ls\" cch=%zu flags=0x%lX\n", in, cch, flags);
    printf("      live %08lX ", (unsigned long)hl);
    if (upto) { printf("buf[0..%zu] =", upto-1); for (size_t i = 0; i < upto && i < 12; ++i)
                    printf(" %04X", (unsigned)liveb[i]); }
    printf("\n      ref  %08lX ", (unsigned long)hr);
    if (upto) { printf("buf[0..%zu] =", upto-1); for (size_t i = 0; i < upto && i < 12; ++i)
                    printf(" %04X", (unsigned)refb[i]); }
    printf("\n");
}

static void one(const wchar_t* in, size_t cch, unsigned long flags)
{
    long hl, hr;
    size_t n = cch < BUFCCH ? cch : BUFCCH;
    size_t k;
    int hi = (flags > 0xFF);
    ++cases;
    if (hi) ++case_high; else ++case_by_flag[flags];
    fill(liveb, BUFCCH); fill(refb, BUFCCH);
    hl = (long)canex(liveb, cch, in, flags);
    hr = wia_ref_pathcchcanonicalizeex(refb, cch, in, flags);
    if (hr == (long)0x0BADF00DL) { ++delegated; return; }   /* outside the oracle's domain */
    if (hl != hr) { ++bad; if (hi) ++bad_high; else ++bad_by_flag[flags];
                    show(in, cch, flags, hl, hr, 12); return; }
    /* compare up to and including live's terminator; cch == 0 compares the untouched first cell */
    k = 0;
    while (k < n && liveb[k] != 0) ++k;
    if (k < n) ++k;                                 /* include the terminator */
    if (n == 0) k = 1;                              /* nothing may be written at all */
    if (memcmp(liveb, refb, k * sizeof(wchar_t))) {
        ++bad; if (hi) ++bad_high; else ++bad_by_flag[flags];
        show(in, cch, flags, hl, hr, k < 12 ? k : 12);
    }
}

/* ---- corpora --------------------------------------------------------------------------------- */

static const wchar_t* SHAPES[] = {
    L"", L"\\", L"\\\\", L"\\\\\\", L".", L"..", L"...", L"....", L"a", L"a\\", L"a\\\\",
    L"C:", L"C:\\", L"C:..", L"C:.", L"C:a", L"C:a\\..", L"C:a\\..\\b", L"C:\\..", L"C:\\a\\..",
    L"C:\\a\\..\\..", L"C:\\a\\b\\..", L"C:\\a\\.\\b", L"C:\\a\\\\b", L"C:\\a\\", L"C:\\z..",
    L"C:\\z..\\b", L"C:\\...\\b", L"C:\\a*..", L"C:\\a*.", L"C:\\z  ", L"C:\\ z",
    L"\\a", L"\\a\\..", L"\\..", L"\\a\\..\\b", L"a\\..", L"a\\..\\b", L"..\\b", L".\\b",
    L"a\\..\\..", L"a\\b\\..\\..\\..",
    L"\\\\srv", L"\\\\srv\\", L"\\\\srv\\..", L"\\\\srv\\..\\..", L"\\\\srv\\..\\b",
    L"\\\\srv\\shr", L"\\\\srv\\shr\\..", L"\\\\srv\\shr\\a\\..\\..", L"\\\\srv\\shr\\a\\..\\..\\..",
    L"\\\\..", L"\\\\.", L"\\\\.\\C:\\a", L"\\\\?\\", L"\\\\?\\C:\\a\\..\\b", L"\\\\?\\C:",
    L"\\\\?\\C:?", L"\\\\?\\a\\b", L"\\\\?\\a\\..", L"\\\\?\\UNC\\s\\h\\a", L"\\\\?\\UNC\\s\\h\\..",
    L"\\\\?\\UNC\\", L"\\\\?\\unc\\s\\h", L"C:/a/../b", L"C:\\a/b", L"//?/C:/a/../b",
    L"/a/../b", L"C:/a/..", L"\\\\?\\C:/a/../b",
    L".\\\\\\.", L"\\.\\\\.", L"..\\\\\\.", L"\\..\\\\.", L".\\\\\\..", L"a\\..\\\\.",
    L"\\\\\\..\\.", L"..\\\\\\..", L"\\..\\\\..", L".\\\\.\\", L"\\.\\", L"aa\\.", L"a\\\\.",
    L"C:\\a\\\\\\..", L"C:\\\\..", L"C:\\\\\\..", L"C:\\a\\.\\..", L"C:\\a\\z..\\..",
    /* Is the drive-letter test unicode-aware? The oracle assumes ASCII a-z only, and the 5.8 M
       enumerated cases were all ASCII, so the assumption was never tested. If the live function
       accepts a Cyrillic letter as a drive letter -- its prefix test calls an indirect character
       predicate, which could be IsCharAlphaW -- these cases say so. */
    L"\\\\?\\\x042F:\\a\\..\\b", L"\x042F:", L"\x042F:\\..", L"\x042F:\x0430\\..", L"\\\\?\\\x042F:?",
    L"\\\\?\\1:\\a\\..\\b", L"1:", L"1:\\..", L"\\\\?\\:\\a", L"\x042F:\\a\\..",
    L"\\\\?\\\x00C5:\\a\\..", L"\x00E5:\\a\\..", L"\\\\?\\unc\\\x042F\\h\\..",
    /* and whether the separator test is only 0x5C: some APIs also accept U+FF3C or U+2215.
       The pieces are concatenated because a hex escape swallows a following hex digit. */
    L"C:\x00A5" L"a\x00A5" L"..", L"C:\xFF3C" L"a\xFF3C" L"..", L"C:\x2215" L"a\x2215" L".."
};
#define NSHAPES ((int)(sizeof(SHAPES)/sizeof(SHAPES[0])))

static unsigned long long rs = 0x5150243ull;
static unsigned rnd(unsigned m){ rs ^= rs<<13; rs ^= rs>>7; rs ^= rs<<17; return (unsigned)(rs % m); }

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    canex = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!canex) { printf("cannot resolve PathCchCanonicalizeEx\n"); return 1; }

    printf("=== 1. the shape corpus at a generous cch, flags 0 ===\n");
    for (int i = 0; i < NSHAPES; ++i) one(SHAPES[i], 0x8000, 0);
    printf("    %lld cases, %lld bad\n", cases, bad);

    printf("\n=== 2. the shape corpus x all 128 flag values ===\n");
    { long long c0 = cases, b0 = bad;
      for (unsigned long f = 0; f < 128; ++f)
          for (int i = 0; i < NSHAPES; ++i) one(SHAPES[i], 0x8000, f);
      printf("    %lld cases, %lld bad\n", cases-c0, bad-b0); }

    printf("\n=== 3. flag bits above the documented seven ===\n");
    { long long c0 = cases, b0 = bad;
      static const unsigned long F[] = { 0x80, 0x100, 0x200, 0x8000, 0x10000, 0x80000000,
                                         0xFFFFFF80, 0xFFFFFFFF, 0x88, 0xA0, 0xC0, 0x120 };
      for (int fi = 0; fi < 12; ++fi)
          for (int i = 0; i < NSHAPES; ++i) one(SHAPES[i], 0x8000, F[fi]);
      printf("    %lld cases, %lld bad\n", cases-c0, bad-b0); }

    printf("\n=== 4. the cch sweep: 0..24 and 0x7FFF..0x8001 on every shape, flags 0 and 0x20 ===\n");
    { long long c0 = cases, b0 = bad;
      static const unsigned long F[] = { 0, 0x20, 0x08, 0x40 };
      for (int fi = 0; fi < 4; ++fi)
          for (int i = 0; i < NSHAPES; ++i) {
              for (size_t c = 0; c <= 24; ++c) one(SHAPES[i], c, F[fi]);
              one(SHAPES[i], 0x7FFF, F[fi]);
              one(SHAPES[i], 0x8000, F[fi]);
              one(SHAPES[i], 0x8001, F[fi]);
              one(SHAPES[i], 0x10000, F[fi]);
              one(SHAPES[i], (size_t)-1, F[fi]);
          }
      printf("    %lld cases, %lld bad\n", cases-c0, bad-b0); }

    printf("\n=== 5. the two length caps, swept across the boundary on every relevant flag ===\n");
    { long long c0 = cases, b0 = bad;
      static wchar_t in[9000];
      static const unsigned long F[] = { 0, 0x01, 0x03, 0x05, 0x08, 0x10, 0x20, 0x40, 0x30, 0x48 };
      for (int fi = 0; fi < 10; ++fi) {
          for (int n = 250; n <= 268; ++n) {                 /* short components, long result */
              int k = 0;
              in[k++] = L'C'; in[k++] = L':';
              while (k < n) { in[k++] = L'\\'; if (k < n) in[k++] = L'a'; }
              in[k] = 0;
              one(in, 0x8000, F[fi]);
              one(in, 300, F[fi]);
          }
          for (int n = 252; n <= 262; ++n) {                  /* one long component */
              int k = 0;
              in[k++] = L'C'; in[k++] = L':'; in[k++] = L'\\';
              for (int i = 0; i < n; ++i) in[k++] = L'a';
              in[k] = 0;
              one(in, 0x8000, F[fi]);
          }
          {   /* a long path whose RESULT is short: the cap is on the running output */
              int k = 0;
              in[k++] = L'C'; in[k++] = L':'; in[k++] = L'\\';
              for (int i = 0; i < 400; ++i) in[k++] = L'a';
              in[k++] = L'\\'; in[k++] = L'.'; in[k++] = L'.';
              in[k] = 0;
              one(in, 0x8000, F[fi]);
          }
          {   /* many short components then a pop back to nothing */
              int k = 0;
              for (int i = 0; i < 200; ++i) { in[k++] = L'a'; in[k++] = L'\\'; }
              for (int i = 0; i < 200; ++i) { in[k++] = L'.'; in[k++] = L'.'; in[k++] = L'\\'; }
              in[k] = 0;
              one(in, 0x8000, F[fi]);
          }
      }
      printf("    %lld cases, %lld bad\n", cases-c0, bad-b0); }

    printf("\n=== 6. exhaustive: every string to length 9 over { \\ . a : }, flags 0 ===\n");
    { long long c0 = cases, b0 = bad;
      wchar_t buf[16];
      static const wchar_t* A = L"\\.a:";
      for (int len = 0; len <= 9; ++len) {
          long long total = 1; for (int i = 0; i < len; ++i) total *= 4;
          for (long long v = 0; v < total; ++v) {
              long long x = v;
              for (int i = 0; i < len; ++i) { buf[i] = A[x & 3]; x >>= 2; }
              buf[len] = 0;
              one(buf, 0x8000, 0);
          }
      }
      printf("    %lld cases, %lld bad\n", cases-c0, bad-b0); }

    printf("\n=== 7. exhaustive: every string to length 7 over { \\ ? U C a . : / * }, flags 0 ===\n");
    { long long c0 = cases, b0 = bad;
      wchar_t buf[16];
      static const wchar_t* A = L"\\?UCa.:/*";
      int base = 9;
      for (int len = 0; len <= 7; ++len) {
          long long total = 1; for (int i = 0; i < len; ++i) total *= base;
          for (long long v = 0; v < total; ++v) {
              long long x = v;
              for (int i = 0; i < len; ++i) { buf[i] = A[x % base]; x /= base; }
              buf[len] = 0;
              one(buf, 0x8000, 0);
          }
      }
      printf("    %lld cases, %lld bad\n", cases-c0, bad-b0); }

    printf("\n=== 8. random paths x random flags x random cch ===\n");
    { long long c0 = cases, b0 = bad;
      static const wchar_t* A = L"\\\\..aC:/*zU?N ";
      static const unsigned long F[] = { 0,0,0,0, 0x01,0x03,0x05,0x08,0x10,0x20,0x40,0x30,0x48,0x28,0x60,0x68 };
      wchar_t buf[600];
      for (long long i = 0; i < 400000; ++i) {
          int len = (int)rnd(120);
          for (int k = 0; k < len; ++k) buf[k] = A[rnd(14)];
          buf[len] = 0;
          unsigned long f = F[rnd(16)];
          size_t cch = (rnd(4) == 0) ? (size_t)rnd(40) : 0x8000;
          one(buf, cch, f);
      }
      printf("    %lld cases, %lld bad\n", cases-c0, bad-b0); }

    printf("\n=== mismatches per flag value (only the ones that ever differ) ===\n");
    for (int f = 0; f < 256; ++f)
        if (bad_by_flag[f]) printf("    flags 0x%02X : %lld bad of %lld\n", f, bad_by_flag[f], case_by_flag[f]);
    if (bad_high) printf("    flags > 0xFF : %lld bad of %lld\n", bad_high, case_high);
    printf("\n=== flag values with cases and NO mismatch ===\n    ");
    for (int f = 0; f < 256; ++f) if (case_by_flag[f] && !bad_by_flag[f]) printf("0x%02X ", f);
    printf("\n");

    printf("\n=== TOTAL: %lld cases, %lld mismatches, %lld delegated (flags != 0) ===\n",
           cases, bad, delegated);
    return bad != 0;
}
