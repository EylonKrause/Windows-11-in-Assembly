/* changes/242-pathcchappendex/correctness.c
   THE GATE for change 242: impl.asm against reference.c against the LIVE kernelbase exports, three ways,
   for both PathCchAppendEx and PathCchCombineEx.

   WHAT IS COMPARED. The HRESULT, the returned string, its terminator, and -- for cch 0 against cch 1 --
   whether anything was written at all. Every destination is poison-filled first, and a CANARY past the
   buffer proves our implementation never writes outside the cch it was given. What is NOT compared is
   the buffer beyond the terminator: the shipped code canonicalises in the caller's buffer and leaves its
   own scratch behind, which no vectorised store can reproduce (see change 243's gate for the same
   decision and the same reason).

   APPEND IS IN PLACE, so each of the three calls gets its own copy of the base seeded into its own
   buffer -- and that is also the property the canary is guarding, because the base, the answer and the
   joined string in between are three different lengths.

   THE DOMAIN IS dwFlags == 0; nonzero flags tail-jump to the original, so for those the test is that
   ours equals live EXACTLY, debris included, which also proves the dispatch.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef HRESULT (WINAPI *FAPP)(PWSTR, size_t, PCWSTR, ULONG);
typedef HRESULT (WINAPI *FCMB)(PWSTR, size_t, PCWSTR, PCWSTR, ULONG);
static FAPP app;
static FCMB cmb;

extern long wia_pathcchappendex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern long wia_pathcchcombineex(wchar_t*, size_t, const wchar_t*, const wchar_t*, unsigned long);
extern void wia_pcap_set_fallback(void*);
extern void wia_pccb_set_fallback(void*);
extern long wia_ref_pathcchappendex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern long wia_ref_pathcchcombineex(wchar_t*, size_t, const wchar_t*, const wchar_t*, unsigned long);

#define REF_DELEGATED ((long)0x0BADF00DL)
#define POISON 0xCDCD
#define CANARY 0xA5A5
#define BUF    0x8400
#define GUARD  64

static wchar_t liveb[BUF+GUARD], refb[BUF+GUARD], oursb[BUF+GUARD];
static long long cases, bad, delegated;
static int shown;

static void seed(wchar_t* b, const wchar_t* base)
{
    for (size_t i = 0; i < BUF; ++i) b[i] = POISON;
    for (size_t i = BUF; i < BUF+GUARD; ++i) b[i] = CANARY;
    if (base) memcpy(b, base, (wcslen(base)+1)*sizeof(wchar_t));
}

static void fail(const char* which, const wchar_t* a, const wchar_t* m, size_t cch,
                 unsigned long flags, long hl, long hr, long ho, const char* why)
{
    ++bad;
    if (shown >= 25) return;
    ++shown;
    printf("    %s MISMATCH (%s) \"%ls\" + \"%ls\" cch=%zu flags=0x%lX\n",
           which, why, a ? a : L"(null)", m ? m : L"(null)", cch, flags);
    printf("      live %08lX \"%ls\"\n", (unsigned long)hl, hl ? L"" : liveb);
    printf("      ref  %08lX \"%ls\"\n", (unsigned long)hr, hr ? L"" : refb);
    printf("      ours %08lX \"%ls\"\n", (unsigned long)ho, ho ? L"" : oursb);
}

/* `keep` is how much of the buffer the test itself seeded, for Append that is the base and its
   terminator, which are legitimately not poison. Everything past max(cch, keep) must be untouched, and
   the canary past the buffer must be intact whatever cch says. */
static int guard_ok(size_t cch, size_t keep)
{
    size_t n = cch < BUF ? cch : BUF;
    if (keep > n) n = keep;
    for (size_t i = 0; i < GUARD; ++i) if (oursb[BUF+i] != CANARY) return 0;
    for (size_t i = n; i < BUF; ++i)    if (oursb[i] != POISON)    return 0;
    return 1;
}

static size_t upto(size_t cch)
{
    size_t n = cch < BUF ? cch : BUF, k = 0;
    while (k < n && liveb[k] != 0) ++k;
    if (k < n) ++k;
    if (n == 0) k = 1;
    return k;
}

static void one(const wchar_t* base, const wchar_t* more, size_t cch, unsigned long flags)
{
    long hl, hr, ho;
    size_t k;
    ++cases;

    /* ---- Append, in place ---- */
    seed(liveb, base); seed(refb, base); seed(oursb, base);
    hl = (long)app(liveb, cch, more, flags);
    hr = wia_ref_pathcchappendex(refb, cch, more, flags);
    ho = wia_pathcchappendex(oursb, cch, more, flags);
    if (!guard_ok(cch, base ? wcslen(base)+1 : 0)) fail("APPEND", base, more, cch, flags, hl, hr, ho, "canary");
    else if (hr == REF_DELEGATED) {
        ++delegated;
        if (ho != hl || memcmp(oursb, liveb, (cch < BUF ? cch : BUF)*sizeof(wchar_t)))
            fail("APPEND", base, more, cch, flags, hl, hr, ho, "delegation");
    } else if (hl != hr) fail("APPEND", base, more, cch, flags, hl, hr, ho, "oracle vs live");
    else if (ho != hl)   fail("APPEND", base, more, cch, flags, hl, hr, ho, "ours vs live HRESULT");
    else {
        k = upto(cch);
        if (memcmp(liveb, refb, k*2))  fail("APPEND", base, more, cch, flags, hl, hr, ho, "oracle string");
        else if (memcmp(liveb, oursb, k*2)) fail("APPEND", base, more, cch, flags, hl, hr, ho, "ours string");
    }

    /* ---- Combine, separate output ---- */
    ++cases;
    seed(liveb, 0); seed(refb, 0); seed(oursb, 0);
    hl = (long)cmb(liveb, cch, base, more, flags);
    hr = wia_ref_pathcchcombineex(refb, cch, base, more, flags);
    ho = wia_pathcchcombineex(oursb, cch, base, more, flags);
    if (!guard_ok(cch, 0)) fail("COMBINE", base, more, cch, flags, hl, hr, ho, "canary");
    else if (hr == REF_DELEGATED) {
        ++delegated;
        if (ho != hl || memcmp(oursb, liveb, (cch < BUF ? cch : BUF)*sizeof(wchar_t)))
            fail("COMBINE", base, more, cch, flags, hl, hr, ho, "delegation");
    } else if (hl != hr) fail("COMBINE", base, more, cch, flags, hl, hr, ho, "oracle vs live");
    else if (ho != hl)   fail("COMBINE", base, more, cch, flags, hl, hr, ho, "ours vs live HRESULT");
    else {
        k = upto(cch);
        if (memcmp(liveb, refb, k*2))  fail("COMBINE", base, more, cch, flags, hl, hr, ho, "oracle string");
        else if (memcmp(liveb, oursb, k*2)) fail("COMBINE", base, more, cch, flags, hl, hr, ho, "ours string");
    }
}

static const wchar_t* S[] = {
    L"", L"\\", L"\\\\", L"\\\\\\", L"\\\\\\\\", L"a", L"a\\", L"ab", L"abc",
    L"C:", L"C:\\", L"C:a", L"C:\\a", L"C:\\a\\", L"C:\\a\\b", L"C:\\a\\\\b",
    L"\\a", L"\\a\\b", L"\\\\srv", L"\\\\srv\\", L"\\\\srv\\shr", L"\\\\srv\\shr\\",
    L"\\\\srv\\shr\\a", L"D:\\b", L".", L"..", L"...", L"....", L".\\a", L"..\\a", L"a\\..",
    L"z..", L"a*.", L"*", L"\\\\?", L"\\\\?a", L"\\\\?\\", L"\\\\?\\C:", L"\\\\?\\C:\\a",
    L"\\\\?\\C:\\a\\b", L"\\\\?\\a", L"\\\\?\\a\\b", L"\\\\?\\UNC", L"\\\\?\\UNC\\",
    L"\\\\?\\UNC\\s", L"\\\\?\\UNC\\s\\h", L"\\\\?\\UNC\\s\\h\\a", L"\\\\.", L"\\\\.\\C:",
    L"\\\\.\\C:\\a", L"C:/a", L"a/b", L"\\b", L"\\\\b", L"b:", L"\\b:", L"..\\..",
    L"C:\\a\\..\\b", L"?\\C:\\a", L"?", L"?\\", L"UNC\\s\\h", L"unc\\s\\h",
    L"\x00C5:\\a", L"\x00C5:", L"\\\\?\\\x00C5:\\a"
};
#define NS ((int)(sizeof(S)/sizeof(S[0])))

static unsigned long long rs = 0x242242242ull;
static unsigned rnd(unsigned m){ rs ^= rs<<13; rs ^= rs>>7; rs ^= rs<<17; return (unsigned)(rs % m); }

static void section(const char* n){ printf("  %s\n", n); shown = 0; }
static void report(long long c0, long long b0){ printf("    %lld calls, %lld bad\n", cases-c0, bad-b0); }

int main(void){
    long long c0, b0;
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    app = (FAPP)GetProcAddress(hk, "PathCchAppendEx");
    cmb = (FCMB)GetProcAddress(hk, "PathCchCombineEx");
    if (!app || !cmb) { printf("cannot resolve the exports\n"); return 1; }
    wia_pcap_set_fallback((void*)app);
    wia_pccb_set_fallback((void*)cmb);

    printf("=== 242 correctness: impl.asm vs reference.c vs live, both exports ===\n");

    section("1. the shape corpus crossed with itself, generous cch");
    c0 = cases; b0 = bad;
    for (int i = 0; i < NS; ++i) for (int j = 0; j < NS; ++j) one(S[i], S[j], 0x8000, 0);
    report(c0, b0);

    section("2. the same crossed corpus x every cch from 0 to 20");
    c0 = cases; b0 = bad;
    for (int i = 0; i < NS; ++i) for (int j = 0; j < NS; ++j)
        for (size_t c = 0; c <= 20; ++c) one(S[i], S[j], c, 0);
    report(c0, b0);

    section("3. the crossed corpus around the cch extremes");
    c0 = cases; b0 = bad;
    { static const size_t C[] = { 0x103, 0x104, 0x105, 0x7FFF, 0x8000, 0x8001, 0x10000, (size_t)-1 };
      for (int i = 0; i < NS; ++i) for (int j = 0; j < NS; ++j)
          for (int k = 0; k < 8; ++k) one(S[i], S[j], C[k], 0); }
    report(c0, b0);

    section("4. enumerated: every string to length 3 over { \\ ? U C a : . } crossed with itself");
    c0 = cases; b0 = bad;
    { static wchar_t A[500][8]; int na = 0, base = 7;
      for (int len = 0; len <= 3 && na < 500; ++len) {
          long total = 1; for (int i = 0; i < len; ++i) total *= base;
          for (long v = 0; v < total && na < 500; ++v) {
              long x = v;
              for (int i = 0; i < len; ++i) { A[na][i] = L"\\?UCa:."[x % base]; x /= base; }
              A[na][len] = 0; ++na;
          }
      }
      for (int i = 0; i < na; ++i) for (int j = 0; j < na; ++j) one(A[i], A[j], 0x8000, 0); }
    report(c0, b0);

    section("5. enumerated: every string to length 4 over { \\ . a : } crossed with itself");
    c0 = cases; b0 = bad;
    { static wchar_t A[400][8]; int na = 0;
      for (int len = 0; len <= 4 && na < 400; ++len) {
          long total = 1; for (int i = 0; i < len; ++i) total *= 4;
          for (long v = 0; v < total && na < 400; ++v) {
              long x = v;
              for (int i = 0; i < len; ++i) { A[na][i] = L"\\.a:"[x % 4]; x /= 4; }
              A[na][len] = 0; ++na;
          }
      }
      for (int i = 0; i < na; ++i) for (int j = 0; j < na; ++j) one(A[i], A[j], 0x8000, 0); }
    report(c0, b0);

    section("6. the length caps: a long base, a long more, and joins that pop back short");
    c0 = cases; b0 = bad;
    { static wchar_t b[900], m[900];
      for (int n = 235; n <= 275; ++n) {
          int k = 0;
          b[k++] = L'C'; b[k++] = L':';
          while (k < n) { b[k++] = L'\\'; if (k < n) b[k++] = L'a'; }
          b[k] = 0;
          one(b, L"x", 0x8000, 0);
          one(b, L"..", 0x8000, 0);
          one(b, L"..\\..\\x", 0x8000, 0);
          one(b, L"", 0x8000, 0);
          one(b, L"\\y", 0x8000, 0);
          one(b, b, 0x8000, 0);
      }
      for (int n = 235; n <= 275; ++n) {
          int k = 0;
          while (k < n) { m[k++] = L'y'; if (k < n && (k % 9) == 8) m[k++] = L'\\'; }
          m[k] = 0;
          one(L"C:\\a", m, 0x8000, 0);
          one(L"", m, 0x8000, 0);
          one(L"\\\\srv\\shr", m, 0x8000, 0);
      }
      { int k = 0;
        b[k++] = L'C'; b[k++] = L':'; b[k++] = L'\\';
        for (int i = 0; i < 500; ++i) b[k++] = L'a';
        b[k] = 0;
        one(b, L"..", 0x8000, 0);
        one(b, L"..\\z", 0x8000, 0);
        one(b, L"\\z", 0x8000, 0); }
      { int k = 0;                                   /* a long base popped away by `more` */
        b[k++] = L'C'; b[k++] = L':';
        for (int i = 0; i < 150; ++i) { b[k++] = L'\\'; b[k++] = L'a'; }
        b[k] = 0;
        k = 0;
        for (int i = 0; i < 160; ++i) { m[k++] = L'.'; m[k++] = L'.'; m[k++] = L'\\'; }
        m[k++] = L'z'; m[k] = 0;
        one(b, m, 0x8000, 0); } }
    report(c0, b0);

    section("7. all 128 flag values (nonzero ones must equal live exactly)");
    c0 = cases; b0 = bad;
    for (unsigned long f = 0; f < 128; ++f)
        for (int i = 0; i < NS; ++i) one(S[i], S[(i*7+3) % NS], 0x8000, f);
    report(c0, b0);

    section("8. NULL in every position");
    c0 = cases; b0 = bad;
    {
        /* these use the full-size buffers, because seed() poisons a whole BUF+GUARD window */
        wchar_t* a = liveb; wchar_t* b = refb; wchar_t* c = oursb;
        long h1, h2, h3;
        seed(a, L"C:\\x"); seed(b, L"C:\\x"); seed(c, L"C:\\x");
        h1 = (long)app(a, 0x8000, 0, 0);
        h2 = wia_ref_pathcchappendex(b, 0x8000, 0, 0);
        h3 = wia_pathcchappendex(c, 0x8000, 0, 0);
        printf("    append more=NULL : live %08lX \"%ls\", ref %08lX, ours %08lX \"%ls\"\n",
               (unsigned long)h1, a, (unsigned long)h2, (unsigned long)h3, c);
        if (h1 != h2 || h1 != h3 || wcscmp(a, c)) { ++bad; printf("      MISMATCH\n"); }
        h1 = (long)app(0, 0x8000, L"b", 0);
        h2 = wia_ref_pathcchappendex(0, 0x8000, L"b", 0);
        h3 = wia_pathcchappendex(0, 0x8000, L"b", 0);
        printf("    append path=NULL : live %08lX, ref %08lX, ours %08lX\n",
               (unsigned long)h1, (unsigned long)h2, (unsigned long)h3);
        if (h1 != h2 || h1 != h3) { ++bad; printf("      MISMATCH\n"); }
        seed(a, 0); seed(c, 0);
        h1 = (long)cmb(a, 0x8000, 0, L"b", 0);
        h3 = wia_pathcchcombineex(c, 0x8000, 0, L"b", 0);
        printf("    combine in=NULL  : live %08lX \"%ls\", ours %08lX \"%ls\"\n",
               (unsigned long)h1, a, (unsigned long)h3, c);
        if (h1 != h3 || wcscmp(a, c)) { ++bad; printf("      MISMATCH\n"); }
        seed(a, 0); seed(c, 0);
        h1 = (long)cmb(a, 0x8000, L"C:\\a", 0, 0);
        h3 = wia_pathcchcombineex(c, 0x8000, L"C:\\a", 0, 0);
        printf("    combine more=NULL: live %08lX \"%ls\", ours %08lX \"%ls\"\n",
               (unsigned long)h1, a, (unsigned long)h3, c);
        if (h1 != h3 || wcscmp(a, c)) { ++bad; printf("      MISMATCH\n"); }
        h1 = (long)cmb(0, 0x8000, L"C:\\a", L"b", 0);
        h3 = wia_pathcchcombineex(0, 0x8000, L"C:\\a", L"b", 0);
        printf("    combine out=NULL : live %08lX, ours %08lX\n", (unsigned long)h1, (unsigned long)h3);
        if (h1 != h3) { ++bad; printf("      MISMATCH\n"); }
        seed(a, 0); seed(c, 0);
        h1 = (long)cmb(a, 0x8000, 0, 0, 0);
        h3 = wia_pathcchcombineex(c, 0x8000, 0, 0, 0);
        printf("    combine both NULL: live %08lX \"%ls\", ours %08lX \"%ls\"\n",
               (unsigned long)h1, h1 ? L"" : a, (unsigned long)h3, h3 ? L"" : c);
        if (h1 != h3) { ++bad; printf("      MISMATCH\n"); }
        seed(a, 0); seed(c, 0);
        h1 = (long)cmb(a, 0, 0, 0, 0);
        h3 = wia_pathcchcombineex(c, 0, 0, 0, 0);
        printf("    combine both NULL, cch 0: live %08lX (buf %04X), ours %08lX (buf %04X)\n",
               (unsigned long)h1, (unsigned)a[0], (unsigned long)h3, (unsigned)c[0]);
        if (h1 != h3 || a[0] != c[0]) { ++bad; printf("      MISMATCH\n"); }
        cases += 7;
    }
    report(c0, b0);

    section("9. random pairs x random cch x random flags");
    c0 = cases; b0 = bad;
    { static const wchar_t* A = L"\\\\..aC:/*zU?N \x00C5";
      static wchar_t x[300], y[300];
      for (long long i = 0; i < 120000; ++i) {
          int lx = (int)rnd(40), ly = (int)rnd(40);
          for (int k = 0; k < lx; ++k) x[k] = A[rnd(16)];
          for (int k = 0; k < ly; ++k) y[k] = A[rnd(16)];
          x[lx] = 0; y[ly] = 0;
          one(x, y, (rnd(4) == 0) ? (size_t)rnd(48) : 0x8000, (rnd(8) == 0) ? rnd(128) : 0);
      } }
    report(c0, b0);

    printf("\n=== TOTAL: %lld calls, %lld MISMATCHES, %lld delegated ===\n", cases, bad, delegated);
    if (bad) { printf("FAILED\n"); return 1; }
    printf("PASS\n");
    return 0;
}
