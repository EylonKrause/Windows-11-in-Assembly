/* changes/242-pathcchappendex/probes/refcheck2.c
   Validate reference.c -- the ORACLE for change 242 -- against the live exports before any assembly
   exists, the way probes/refcheck.c did for change 243.

   probes/compose.c already showed the composition holds. This goes further in the places a join rule can
   still be wrong without the composition noticing: the cch sweep on both functions (Append works IN
   PLACE, so its buffer holds the base on entry and the answer on exit), the length caps, NULL in every
   position, and whether Append and Combine share the argument checks change 243 measured.

   THE COMPARISON IS THE HRESULT, THE STRING AND ITS TERMINATOR, plus whether anything was written at
   all -- cch 0 must leave the buffer untouched. What is not compared is the buffer past the terminator,
   for the reason change 243's gate records: the shipped code works in the caller's buffer and leaves its
   own scratch behind, which no vectorised store can reproduce.

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

extern long wia_ref_pathcchappendex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern long wia_ref_pathcchcombineex(wchar_t*, size_t, const wchar_t*, const wchar_t*, unsigned long);

#define POISON 0xCDCD
#define BUF    0x8400

static wchar_t liveb[BUF], refb[BUF];
static long long cases, bad_a, bad_c;
static int shown_a, shown_c;

static void fill(wchar_t* b){ for (int i = 0; i < BUF; ++i) b[i] = POISON; }

static size_t cmp_upto(size_t cch)
{
    size_t n = cch < BUF ? cch : BUF, k = 0;
    while (k < n && liveb[k] != 0) ++k;
    if (k < n) ++k;
    if (n == 0) k = 1;
    return k;
}

static void one(const wchar_t* base, const wchar_t* more, size_t cch)
{
    long hl, hr;
    size_t k;
    ++cases;

    /* Append: the base is the buffer */
    fill(liveb); fill(refb);
    if (cch) { memcpy(liveb, base, (wcslen(base)+1)*2); memcpy(refb, base, (wcslen(base)+1)*2); }
    else     { memcpy(liveb, base, (wcslen(base)+1)*2); memcpy(refb, base, (wcslen(base)+1)*2); }
    hl = (long)app(liveb, cch, more, 0);
    hr = wia_ref_pathcchappendex(refb, cch, more, 0);
    k = cmp_upto(cch);
    if (hl != hr || memcmp(liveb, refb, k*2)) {
        ++bad_a;
        if (shown_a < 20) {
            ++shown_a;
            printf("    APPEND  \"%ls\" + \"%ls\" cch=%zu: live %08lX \"%ls\"  ref %08lX \"%ls\"\n",
                   base, more, cch, (unsigned long)hl, hl ? L"" : liveb,
                   (unsigned long)hr, hr ? L"" : refb);
        }
    }

    /* Combine: a separate output */
    fill(liveb); fill(refb);
    hl = (long)cmb(liveb, cch, base, more, 0);
    hr = wia_ref_pathcchcombineex(refb, cch, base, more, 0);
    k = cmp_upto(cch);
    if (hl != hr || memcmp(liveb, refb, k*2)) {
        ++bad_c;
        if (shown_c < 20) {
            ++shown_c;
            printf("    COMBINE \"%ls\" + \"%ls\" cch=%zu: live %08lX \"%ls\"  ref %08lX \"%ls\"\n",
                   base, more, cch, (unsigned long)hl, hl ? L"" : liveb,
                   (unsigned long)hr, hr ? L"" : refb);
        }
    }
}

static const wchar_t* S[] = {
    L"", L"\\", L"\\\\", L"\\\\\\", L"a", L"a\\", L"ab", L"C:", L"C:\\", L"C:a", L"C:\\a", L"C:\\a\\",
    L"C:\\a\\b", L"\\a", L"\\a\\b", L"\\\\srv", L"\\\\srv\\", L"\\\\srv\\shr", L"\\\\srv\\shr\\",
    L"\\\\srv\\shr\\a", L"D:\\b", L".", L"..", L"...", L".\\a", L"..\\a", L"a\\..", L"z..",
    L"\\\\?", L"\\\\?a", L"\\\\?\\", L"\\\\?\\C:", L"\\\\?\\C:\\a", L"\\\\?\\UNC\\s\\h", L"\\\\.",
    L"\\\\.\\C:", L"C:/a", L"a/b", L"*", L"a*.", L"\\b", L"\\\\b", L"b:", L"\\b:", L"..\\..",
    L"C:\\a\\..\\b",
    /* the extended-prefix bases whose root is the question: which of these has one at all? */
    L"\\\\?\\a", L"\\\\?\\a\\b", L"\\\\?\\UNC", L"\\\\?\\UNC\\", L"\\\\?\\UNC\\s",
    L"\\\\?\\C:\\a\\b", L"\\\\?\\\\", L"\\\\?\\C", L"\\\\?\\:", L"\\\\.\\C:\\a", L"\\\\.\\a\\b"
};
#define NS ((int)(sizeof(S)/sizeof(S[0])))

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    app = (FAPP)GetProcAddress(hk, "PathCchAppendEx");
    cmb = (FCMB)GetProcAddress(hk, "PathCchCombineEx");
    if (!app || !cmb) { printf("cannot resolve the exports\n"); return 1; }

    printf("=== 1. the shape corpus crossed with itself, generous cch ===\n");
    for (int i = 0; i < NS; ++i) for (int j = 0; j < NS; ++j) one(S[i], S[j], 0x8000);
    printf("    %lld pairs: %lld append, %lld combine\n", cases, bad_a, bad_c);

    printf("\n=== 2. the same crossed corpus x every cch from 0 to 20 ===\n");
    { long long c0 = cases, a0 = bad_a, m0 = bad_c;
      for (int i = 0; i < NS; ++i)
          for (int j = 0; j < NS; ++j)
              for (size_t c = 0; c <= 20; ++c) one(S[i], S[j], c);
      printf("    %lld cases: %lld append, %lld combine\n", cases-c0, bad_a-a0, bad_c-m0); }

    printf("\n=== 3. enumerated: every string to length 3 over { \\ ? U C a : . } crossed ===\n");
    { long long c0 = cases, a0 = bad_a, m0 = bad_c;
      static wchar_t A[500][8]; int na = 0, base = 7;
      for (int len = 0; len <= 3 && na < 500; ++len) {
          long total = 1; for (int i = 0; i < len; ++i) total *= base;
          for (long v = 0; v < total && na < 500; ++v) {
              long x = v;
              for (int i = 0; i < len; ++i) { A[na][i] = L"\\?UCa:."[x % base]; x /= base; }
              A[na][len] = 0; ++na;
          }
      }
      for (int i = 0; i < na; ++i) for (int j = 0; j < na; ++j) one(A[i], A[j], 0x8000);
      printf("    %lld pairs from %d strings: %lld append, %lld combine\n",
             cases-c0, na, bad_a-a0, bad_c-m0); }

    printf("\n=== 4. the length caps: a long base, a long more, and a join that pops back short ===\n");
    { long long c0 = cases, a0 = bad_a, m0 = bad_c;
      static wchar_t b[600], m[600];
      for (int n = 240; n <= 270; ++n) {
          int k = 0;
          b[k++] = L'C'; b[k++] = L':';
          while (k < n) { b[k++] = L'\\'; if (k < n) b[k++] = L'a'; }
          b[k] = 0;
          one(b, L"x", 0x8000);
          one(b, L"..", 0x8000);
          one(b, L"..\\..\\x", 0x8000);
          one(b, L"", 0x8000);
      }
      for (int n = 240; n <= 270; ++n) {
          int k = 0;
          while (k < n) { m[k++] = L'y'; if (k < n && (k % 9) == 8) m[k++] = L'\\'; }
          m[k] = 0;
          one(L"C:\\a", m, 0x8000);
          one(L"", m, 0x8000);
      }
      /* a long base whose join pops all the way back */
      { int k = 0;
        b[k++] = L'C'; b[k++] = L':'; b[k++] = L'\\';
        for (int i = 0; i < 400; ++i) b[k++] = L'a';
        b[k] = 0;
        one(b, L"..", 0x8000);
        one(b, L"..\\z", 0x8000); }
      printf("    %lld cases: %lld append, %lld combine\n", cases-c0, a0 == bad_a ? 0 : bad_a-a0,
             bad_c-m0); }

    printf("\n=== 5. NULL in every position, and whether it faults or returns ===\n");
    {
        long hl, hr; int lf, rf;
        static wchar_t buf[64];
#define TRYL(expr) do { lf = 0; __try { hl = (long)(expr); } __except(1) { lf = 1; } } while (0)
#define TRYR(expr) do { rf = 0; __try { hr = (long)(expr); } __except(1) { rf = 1; } } while (0)
        wcscpy(buf, L"C:\\a");
        TRYL(app(buf, 0x8000, 0, 0));
        wcscpy(buf, L"C:\\a");
        TRYR(wia_ref_pathcchappendex(buf, 0x8000, 0, 0));
        printf("    append more=NULL : live %s %08lX, ref %s %08lX\n",
               lf?"FAULT":"ret", (unsigned long)hl, rf?"FAULT":"ret", (unsigned long)hr);
        if (lf != rf || (!lf && hl != hr)) { ++bad_a; printf("      MISMATCH\n"); }
        TRYL(app(0, 0x8000, L"b", 0));
        TRYR(wia_ref_pathcchappendex(0, 0x8000, L"b", 0));
        printf("    append path=NULL : live %s %08lX, ref %s %08lX\n",
               lf?"FAULT":"ret", (unsigned long)hl, rf?"FAULT":"ret", (unsigned long)hr);
        if (lf != rf || (!lf && hl != hr)) { ++bad_a; printf("      MISMATCH\n"); }
        TRYL(cmb(buf, 0x8000, 0, L"b", 0));
        TRYR(wia_ref_pathcchcombineex(buf, 0x8000, 0, L"b", 0));
        printf("    combine in=NULL  : live %s %08lX \"%ls\", ref %s %08lX\n",
               lf?"FAULT":"ret", (unsigned long)hl, lf?L"":buf, rf?"FAULT":"ret", (unsigned long)hr);
        if (lf != rf || (!lf && hl != hr)) { ++bad_c; printf("      MISMATCH\n"); }
        TRYL(cmb(buf, 0x8000, L"C:\\a", 0, 0));
        TRYR(wia_ref_pathcchcombineex(buf, 0x8000, L"C:\\a", 0, 0));
        printf("    combine more=NULL: live %s %08lX, ref %s %08lX\n",
               lf?"FAULT":"ret", (unsigned long)hl, rf?"FAULT":"ret", (unsigned long)hr);
        if (lf != rf || (!lf && hl != hr)) { ++bad_c; printf("      MISMATCH\n"); }
        TRYL(cmb(0, 0x8000, L"C:\\a", L"b", 0));
        TRYR(wia_ref_pathcchcombineex(0, 0x8000, L"C:\\a", L"b", 0));
        printf("    combine out=NULL : live %s %08lX, ref %s %08lX\n",
               lf?"FAULT":"ret", (unsigned long)hl, rf?"FAULT":"ret", (unsigned long)hr);
        if (lf != rf || (!lf && hl != hr)) { ++bad_c; printf("      MISMATCH\n"); }
        TRYL(cmb(buf, 0x8000, 0, 0, 0));
        TRYR(wia_ref_pathcchcombineex(buf, 0x8000, 0, 0, 0));
        printf("    combine both NULL: live %s %08lX \"%ls\", ref %s %08lX\n",
               lf?"FAULT":"ret", (unsigned long)hl, lf?L"":buf, rf?"FAULT":"ret", (unsigned long)hr);
        if (lf != rf || (!lf && hl != hr)) { ++bad_c; printf("      MISMATCH\n"); }
    }

    printf("\n=== TOTAL: %lld cases, %lld append mismatches, %lld combine mismatches ===\n",
           cases, bad_a, bad_c);
    return (bad_a || bad_c) ? 1 : 0;
}
