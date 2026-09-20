/* changes/243-pathcchcanonicalizeex/correctness.c
   THE GATE for change 243: impl.asm against reference.c against the LIVE kernelbase export, three ways,
   over every corner the contract has.

   WHAT IS COMPARED. The HRESULT, the returned string, its terminator, and whether anything was written
   at all -- every destination is poison-filled first, because several rules here are about what is NOT
   written: cch == 0 leaves the buffer untouched while cch == 1 empties it, and every error path empties
   it. What is NOT compared is the buffer BEYOND the terminator: the shipped implementation walks the
   path directly in the caller's buffer and truncates as it pops, leaving its own scratch behind
   ("C:\a\.." comes back as "C:\" followed by the leftover "\" of the "C:\a\" it built), and
   reproducing that would forbid any vectorised store. Instead, a CANARY after the buffer proves our
   implementation never writes outside the cch it was given -- which is the property that actually
   matters.

   THE DOMAIN IS dwFlags == 0. Nonzero flags tail-jump to the original implementation, so for those the
   test is that ours equals live EXACTLY, debris included -- which also proves the dispatch itself.

   Flag 0x01 is why the domain stops there: it is not a post-step but a different backward walk
   ("C:a\.." is "\" with flags 0 and "C:a\" with 0x01), i.e. a second contract. See RESULTS.md.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
static CANEX canex;

extern long wia_pathcchcanonicalizeex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern void wia_pccx_set_fallback(void*);
extern long wia_ref_pathcchcanonicalizeex(wchar_t*, size_t, const wchar_t*, unsigned long);

#define REF_DELEGATED ((long)0x0BADF00DL)
#define POISON  0xCDCD
#define CANARY  0xA5A5
#define BUFCCH  0x8200
#define GUARD   64

static wchar_t liveb[BUFCCH + GUARD], refb[BUFCCH + GUARD], oursb[BUFCCH + GUARD];
static long long cases, bad, delegated_cases;
static int shown;

static void fill(wchar_t* b)
{
    for (size_t i = 0; i < BUFCCH; ++i) b[i] = POISON;
    for (size_t i = BUFCCH; i < BUFCCH + GUARD; ++i) b[i] = CANARY;
}

static void dump(const char* who, const wchar_t* b, long hr, size_t k)
{
    printf("      %-5s %08lX  ", who, (unsigned long)hr);
    for (size_t i = 0; i < k && i < 14; ++i) printf(" %04X", (unsigned)b[i]);
    printf("\n");
}

static void fail(const wchar_t* in, size_t cch, unsigned long flags,
                 long hl, long hr, long ho, size_t k, const char* why)
{
    ++bad;
    if (shown >= 30) return;
    ++shown;
    printf("    MISMATCH (%s) in \"%ls\" cch=%zu flags=0x%lX\n", why, in, cch, flags);
    dump("live", liveb, hl, k);
    dump("ref",  refb,  hr, k);
    dump("ours", oursb, ho, k);
}

static void one(const wchar_t* in, size_t cch, unsigned long flags)
{
    long hl, hr, ho;
    size_t n = cch < BUFCCH ? cch : BUFCCH;
    size_t k, i;
    ++cases;
    fill(liveb); fill(refb); fill(oursb);

    hl = (long)canex(liveb, cch, in, flags);
    hr = wia_ref_pathcchcanonicalizeex(refb, cch, in, flags);
    ho = wia_pathcchcanonicalizeex(oursb, cch, in, flags);

    /* the canary: ours must not write past the cch it was given, ever */
    for (i = 0; i < GUARD; ++i)
        if (oursb[BUFCCH + i] != CANARY) { fail(in, cch, flags, hl, hr, ho, 14, "canary"); return; }
    if (n < BUFCCH)
        for (i = n; i < BUFCCH; ++i)
            if (oursb[i] != POISON) { fail(in, cch, flags, hl, hr, ho, 14, "wrote past cch"); return; }

    if (hr == REF_DELEGATED) {                  /* outside the domain: ours must BE live */
        ++delegated_cases;
        if (ho != hl || memcmp(oursb, liveb, n * sizeof(wchar_t)))
            fail(in, cch, flags, hl, hr, ho, 14, "delegation");
        return;
    }

    if (hl != hr) { fail(in, cch, flags, hl, hr, ho, 14, "oracle vs live HRESULT"); return; }
    if (ho != hl) { fail(in, cch, flags, hl, hr, ho, 14, "ours vs live HRESULT"); return; }

    k = 0;
    while (k < n && liveb[k] != 0) ++k;
    if (k < n) ++k;                              /* include the terminator */
    if (n == 0) k = 1;                           /* nothing may be written at all */
    if (memcmp(liveb, refb,  k * sizeof(wchar_t))) { fail(in, cch, flags, hl, hr, ho, k<14?k:14, "oracle vs live"); return; }
    if (memcmp(liveb, oursb, k * sizeof(wchar_t))) { fail(in, cch, flags, hl, hr, ho, k<14?k:14, "ours vs live"); return; }
}

/* ---- corpora --------------------------------------------------------------------------------- */

static const wchar_t* SHAPES[] = {
    L"", L"\\", L"\\\\", L"\\\\\\", L"\\\\\\\\", L".", L"..", L"...", L"....", L".....",
    L"a", L"a\\", L"a\\\\", L"ab", L"abc",
    L"C:", L"C:\\", L"C:..", L"C:.", L"C:...", L"C:a", L"C:a\\..", L"C:a\\..\\b", L"C:\\..",
    L"C:\\a\\..", L"C:\\a\\..\\..", L"C:\\a\\b\\..", L"C:\\a\\.\\b", L"C:\\a\\\\b", L"C:\\a\\",
    L"C:\\z..", L"C:\\z..\\b", L"C:\\...\\b", L"C:\\....\\b", L"C:\\a*..", L"C:\\a*.", L"C:\\a*...",
    L"C:\\*.", L"C:\\a.*.", L"C:\\z  ", L"C:\\ z", L"C:\\a.b", L"C:\\.a", L"C:\\..a",
    L"C:\\.\\.\\.", L"C:\\..\\..", L"C:\\\\..", L"C:\\\\\\..", L"C:\\a\\\\\\..", L"C:\\a\\.\\..",
    L"C:\\a\\z..\\..", L"C:\\a\\\\..",
    L"\\a", L"\\a\\..", L"\\..", L"\\a\\..\\b", L"\\a\\..\\..", L"\\.", L"\\.\\", L"\\..\\",
    L"a\\..", L"a\\..\\b", L"..\\b", L".\\b", L"a\\..\\..", L"a\\b\\..\\..\\..", L"aa\\.", L"a\\\\.",
    L"\\\\srv", L"\\\\srv\\", L"\\\\srv\\..", L"\\\\srv\\..\\..", L"\\\\srv\\..\\b",
    L"\\\\srv\\shr", L"\\\\srv\\shr\\", L"\\\\srv\\shr\\..", L"\\\\srv\\shr\\..\\b",
    L"\\\\srv\\shr\\a\\..\\..", L"\\\\srv\\shr\\a\\..\\..\\..", L"\\\\srv\\shr\\a\\..\\..\\..\\..",
    L"\\\\..", L"\\\\.", L"\\\\.\\C:\\a", L"\\\\a\\b\\c\\..\\..",
    L"\\\\?\\", L"\\\\?\\C:\\a\\..\\b", L"\\\\?\\C:", L"\\\\?\\C:?", L"\\\\?\\C:\\..",
    L"\\\\?\\a\\b", L"\\\\?\\a\\..", L"\\\\?\\UNC\\s\\h\\a", L"\\\\?\\UNC\\s\\h\\..",
    L"\\\\?\\UNC\\", L"\\\\?\\UNC", L"\\\\?\\unc\\s\\h", L"\\\\?\\UNC\\s\\..", L"\\\\?\\UNC\\..",
    L"C:/a/../b", L"C:\\a/b", L"//?/C:/a/../b", L"/a/../b", L"C:/a/..", L"\\\\?\\C:/a/../b",
    L".\\\\\\.", L"\\.\\\\.", L"..\\\\\\.", L"\\..\\\\.", L".\\\\\\..", L"a\\..\\\\.",
    L"\\\\\\..\\.", L"..\\\\\\..", L"\\..\\\\..", L".\\\\.\\", L"\\\\.\\", L"\\\\\\.",
    /* the drive-letter predicate is ISO-8859-1, not ASCII and not Unicode: probes/letter.c */
    L"\\\\?\\\x00C5:\\a\\..", L"\x00C5:\\a\\..", L"\x00C5:", L"\x00C5:\\..", L"\x00E5:..",
    L"\\\\?\\\x00D7:\\a\\..", L"\x00D7:\\a\\..", L"\\\\?\\\x00F7:\\a", L"\\\\?\\\x042F:\\a\\..",
    L"\x042F:\\a\\..", L"\\\\?\\1:\\a\\..", L"1:\\..", L"\\\\?\\:\\a", L"\\\\?\\\x00FF:\\a\\..",
    L"\\\\?\\\x0160:\\a\\..", L"\x00C0:\\a\\..\\..",
    /* separators are only 0x5C */
    L"C:\x00A5" L"a\x00A5" L"..", L"C:\xFF3C" L"a\xFF3C" L"..", L"C:\x2215" L"a\x2215" L".."
};
#define NSHAPES ((int)(sizeof(SHAPES)/sizeof(SHAPES[0])))

static unsigned long long rs = 0x243C0DEull;
static unsigned rnd(unsigned m){ rs ^= rs<<13; rs ^= rs>>7; rs ^= rs<<17; return (unsigned)(rs % m); }

static void section(const char* name){ printf("  %s\n", name); shown = 0; }
static void report(long long c0, long long b0)
{
    printf("    %lld cases, %lld bad\n", cases - c0, bad - b0);
}

int main(void){
    long long c0, b0;
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    canex = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!canex) { printf("cannot resolve PathCchCanonicalizeEx\n"); return 1; }
    wia_pccx_set_fallback((void*)canex);          /* nonzero dwFlags go to the original */

    printf("=== 243 correctness: impl.asm vs reference.c vs live ===\n");

    section("1. the shape corpus at a generous cch, flags 0");
    c0 = cases; b0 = bad;
    for (int i = 0; i < NSHAPES; ++i) one(SHAPES[i], 0x8000, 0);
    report(c0, b0);

    section("2. the shape corpus x every cch from 0 to 24, and around the extremes");
    c0 = cases; b0 = bad;
    for (int i = 0; i < NSHAPES; ++i) {
        for (size_t c = 0; c <= 24; ++c) one(SHAPES[i], c, 0);
        one(SHAPES[i], 0x103, 0); one(SHAPES[i], 0x104, 0); one(SHAPES[i], 0x105, 0);
        one(SHAPES[i], 0x7FFF, 0); one(SHAPES[i], 0x8000, 0); one(SHAPES[i], 0x8001, 0);
        one(SHAPES[i], 0x10000, 0); one(SHAPES[i], (size_t)-1, 0);
    }
    report(c0, b0);

    section("3. the shape corpus x all 128 flag values (nonzero ones must equal live exactly)");
    c0 = cases; b0 = bad;
    for (unsigned long f = 0; f < 128; ++f)
        for (int i = 0; i < NSHAPES; ++i) one(SHAPES[i], 0x8000, f);
    report(c0, b0);

    section("4. flag bits above the documented seven");
    c0 = cases; b0 = bad;
    { static const unsigned long F[] = { 0x80, 0x100, 0x8000, 0x80000000, 0xFFFFFFFF, 0x88, 0xC0 };
      for (int fi = 0; fi < 7; ++fi)
          for (int i = 0; i < NSHAPES; ++i) one(SHAPES[i], 0x8000, F[fi]); }
    report(c0, b0);

    section("5. exhaustive: every string to length 10 over { \\ . a }, flags 0");
    c0 = cases; b0 = bad;
    { wchar_t buf[16];
      for (int len = 0; len <= 10; ++len) {
          long long total = 1; for (int i = 0; i < len; ++i) total *= 3;
          for (long long v = 0; v < total; ++v) {
              long long x = v;
              for (int i = 0; i < len; ++i) { buf[i] = L"\\.a"[x % 3]; x /= 3; }
              buf[len] = 0;
              one(buf, 0x8000, 0);
          }
      } }
    report(c0, b0);

    section("6. exhaustive: every string to length 8 over { \\ ? U C a . : }, flags 0");
    c0 = cases; b0 = bad;
    { wchar_t buf[16]; int base = 7;
      for (int len = 0; len <= 8; ++len) {
          long long total = 1; for (int i = 0; i < len; ++i) total *= base;
          for (long long v = 0; v < total; ++v) {
              long long x = v;
              for (int i = 0; i < len; ++i) { buf[i] = L"\\?UCa.:"[x % base]; x /= base; }
              buf[len] = 0;
              one(buf, 0x8000, 0);
          }
      } }
    report(c0, b0);

    section("7. exhaustive: every string to length 7 over { \\ . a * / : }, flags 0");
    c0 = cases; b0 = bad;
    { wchar_t buf[16]; int base = 6;
      for (int len = 0; len <= 7; ++len) {
          long long total = 1; for (int i = 0; i < len; ++i) total *= base;
          for (long long v = 0; v < total; ++v) {
              long long x = v;
              for (int i = 0; i < len; ++i) { buf[i] = L"\\.a*/:"[x % base]; x /= base; }
              buf[len] = 0;
              one(buf, 0x8000, 0);
          }
      } }
    report(c0, b0);

    section("8. the length caps: results and components swept across every boundary");
    c0 = cases; b0 = bad;
    { static wchar_t in[9000];
      for (int n = 1; n <= 300; ++n) {                     /* short components, growing result */
          int k = 0;
          in[k++] = L'C'; in[k++] = L':';
          while (k < n) { in[k++] = L'\\'; if (k < n) in[k++] = L'a'; }
          in[k] = 0;
          one(in, 0x8000, 0);
          one(in, 300, 0);
          one(in, (size_t)n, 0);
          one(in, (size_t)n + 1, 0);
      }
      for (int n = 250; n <= 262; ++n) {                    /* one long component */
          int k = 0;
          in[k++] = L'C'; in[k++] = L':'; in[k++] = L'\\';
          for (int i = 0; i < n; ++i) in[k++] = L'a';
          in[k] = 0;
          one(in, 0x8000, 0);
          one(in, 40, 0);
      }
      {   /* a long path whose result is short: the cap is on the RUNNING output */
          int k = 0;
          in[k++] = L'C'; in[k++] = L':'; in[k++] = L'\\';
          for (int i = 0; i < 400; ++i) in[k++] = L'a';
          in[k++] = L'\\'; in[k++] = L'.'; in[k++] = L'.';
          in[k] = 0;
          one(in, 0x8000, 0);
      }
      {   /* many short components, then popped back to nothing */
          int k = 0;
          for (int i = 0; i < 200; ++i) { in[k++] = L'a'; in[k++] = L'\\'; }
          for (int i = 0; i < 200; ++i) { in[k++] = L'.'; in[k++] = L'.'; in[k++] = L'\\'; }
          in[k] = 0;
          one(in, 0x8000, 0);
      }
      {   /* exactly at the fast path's 256-character edge, with and without a dot component */
          for (int n = 250; n <= 262; ++n) {
              int k = 0;
              for (int i = 0; i < n; ++i) in[k++] = L'a';
              in[k] = 0;
              one(in, 0x8000, 0);
              in[k++] = L'\\'; in[k++] = L'.'; in[k++] = L'.'; in[k] = 0;
              one(in, 0x8000, 0);
          }
      } }
    report(c0, b0);

    section("9. page-edge inputs: a string ending one character before an unmapped page");
    c0 = cases; b0 = bad;
    {   SYSTEM_INFO si; GetSystemInfo(&si);
        size_t pg = si.dwPageSize;
        char* region = (char*)VirtualAlloc(0, pg * 3, MEM_RESERVE, PAGE_NOACCESS);
        if (region) {
            VirtualAlloc(region, pg * 2, MEM_COMMIT, PAGE_READWRITE);
            /* the third page stays NOACCESS: any read past the terminator faults */
            static const wchar_t* T[] = {
                L"C:\\a\\b", L"C:\\a\\..\\b", L"a\\..", L"\\\\srv\\shr\\..", L"C:\\z..",
                L"C:\\a\\.\\b", L"..\\b", L"\\\\?\\C:\\a\\..", L"C:", L".", L"..", L"\\"
            };
            for (int i = 0; i < 12; ++i) {
                size_t len = wcslen(T[i]);
                for (int off = 0; off <= 8; ++off) {
                    wchar_t* p = (wchar_t*)(region + pg*2 - (len + 1 + off) * sizeof(wchar_t));
                    memcpy(p, T[i], (len + 1) * sizeof(wchar_t));
                    one(p, 0x8000, 0);
                    one(p, 8, 0);
                }
            }
            VirtualFree(region, 0, MEM_RELEASE);
        } else printf("    (could not reserve the guard region)\n");
    }
    report(c0, b0);

    section("10. random paths x random cch x random flags");
    c0 = cases; b0 = bad;
    { static const wchar_t* A = L"\\\\..aC:/*zU?N \x00C5";
      static const unsigned long F[] = { 0,0,0,0,0,0, 0x01,0x08,0x10,0x20,0x40,0x30,0x48 };
      wchar_t buf[600];
      for (long long i = 0; i < 300000; ++i) {
          int len = (int)rnd(140);
          for (int k = 0; k < len; ++k) buf[k] = A[rnd(16)];
          buf[len] = 0;
          one(buf, (rnd(4) == 0) ? (size_t)rnd(48) : 0x8000, F[rnd(13)]);
      } }
    report(c0, b0);

    section("11. NULL faults, in both, and the fault is UNWINDABLE");
    /* Both NULLs fault, measured in probes/pccx.c, unlike change 240 which returns E_INVALIDARG --
       so there is nothing to check and the first access reproduces it. What this section really
       proves is that the fault can be UNWOUND: change 241 spent an afternoon on a harness that
       exited with code 5 and no output because a fault inside a PROC with no unwind information
       cannot be unwound, so the caller's __except never ran. If these four lines print, impl.asm's
       PROC FRAME and its true-leaf helpers are right. */
    {
        int lf = 0, of = 0, ln = 0, on = 0;
        __try { canex(liveb, 0x8000, 0, 0); }                 __except(1) { lf = 1; }
        __try { wia_pathcchcanonicalizeex(liveb, 0x8000, 0, 0); } __except(1) { of = 1; }
        __try { canex(0, 0x8000, L"C:\\a", 0); }               __except(1) { ln = 1; }
        __try { wia_pathcchcanonicalizeex(0, 0x8000, L"C:\\a", 0); } __except(1) { on = 1; }
        printf("    input NULL : live %s, ours %s\n", lf ? "faulted" : "returned",
                                                      of ? "faulted" : "returned");
        printf("    output NULL: live %s, ours %s\n", ln ? "faulted" : "returned",
                                                      on ? "faulted" : "returned");
        if (lf != of || ln != on) { ++bad; printf("    MISMATCH: the fault behaviour differs\n"); }
        ++cases; ++cases;
    }

    printf("\n=== TOTAL: %lld cases, %lld MISMATCHES, %lld delegated ===\n",
           cases, bad, delegated_cases);
    if (bad) { printf("FAILED\n"); return 1; }
    printf("PASS\n");
    return 0;
}
