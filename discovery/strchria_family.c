/* discovery/strchria_family.c
   The 8-bit case-insensitive character family: StrChrIA, StrRChrIA, StrCSpnIA, ChrCmpIA (kernelbase,
   shlwapi thunks) -- the A siblings of changes 281-286, which converted the WIDE family.

   WHY. kernelbase!ChrCmpIA (RVA 0x51A10) builds two one-character strings and calls CompareStringA
   with LOCALE_SYSTEM_DEFAULT and NORM_IGNORECASE; StrChrIA (RVA 0x50190) does the same per character
   of the haystack. On a single-byte ANSI code page, whether byte b matches needle n is then a fixed
   relation over 256 x 256 pairs -- if it does not depend on the thread, on context, or on the high
   byte of the WORD needle. Established here:

     1. the cost per character, against the wide forms;
     2. the relation: how many pairs, symmetric?, transitive?, and what the needle NUL matches;
     3. StrChrIA's relation is ChrCmpIA's (and in which argument order);
     4. the WORD needle: is its high byte ignored on a single-byte code page?
     5. thread locale and UI language change nothing?
     6. StrRChrIA's and StrCSpnIA's shapes (end pointer / terminator / return).
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef BOOL  (WINAPI *PCMP)(WORD, WORD);
typedef char* (WINAPI *PCHR)(const char*, WORD);
typedef char* (WINAPI *PRCHR)(const char*, const char*, WORD);
typedef int   (WINAPI *PCSPN)(const char*, const char*);
typedef wchar_t* (WINAPI *PCHRW)(const wchar_t*, wchar_t);
static PCMP cmpA; static PCHR chrA; static PRCHR rchrA; static PCSPN cspnA; static PCHRW chrW;

static LARGE_INTEGER F;
static volatile uint64_t sink;

static double time_chrA(const char* s, WORD n, int N) {
    LARGE_INTEGER a, b; double best = 1e300;
    for (int t = 0; t < 7; ++t) { QueryPerformanceCounter(&a); for (int i = 0; i < N; ++i) sink += (uintptr_t)chrA(s, n); QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)F.QuadPart / N; if (ns < best) best = ns; }
    return best;
}
static double time_chrW(const wchar_t* s, wchar_t n, int N) {
    LARGE_INTEGER a, b; double best = 1e300;
    for (int t = 0; t < 7; ++t) { QueryPerformanceCounter(&a); for (int i = 0; i < N; ++i) sink += (uintptr_t)chrW(s, n); QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)F.QuadPart / N; if (ns < best) best = ns; }
    return best;
}
static double time_cmpA(WORD x, WORD y, int N) {
    LARGE_INTEGER a, b; double best = 1e300;
    for (int t = 0; t < 7; ++t) { QueryPerformanceCounter(&a); for (int i = 0; i < N; ++i) sink += cmpA(x, y); QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)F.QuadPart / N; if (ns < best) best = ns; }
    return best;
}

static unsigned char rel[256][256];      /* rel[n][b]: StrChrIA("b", n) found it */

static const char spin_str[] = "ab\0cdA";
static volatile ptrdiff_t spin_result = -99;
static DWORD WINAPI spin_probe(void* p) { (void)p; const char* r = rchrA(spin_str, spin_str + 6, 'a'); spin_result = r ? r - spin_str : -1; return 0; }

static double time_rchrA(const char* s, const char* e, WORD n, int N) {
    LARGE_INTEGER a, b; double best = 1e300;
    for (int t = 0; t < 7; ++t) { QueryPerformanceCounter(&a); for (int i = 0; i < N; ++i) sink += (uintptr_t)rchrA(s, e, n); QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)F.QuadPart / N; if (ns < best) best = ns; }
    return best;
}
static double time_cspnA(const char* s, const char* set, int N) {
    LARGE_INTEGER a, b; double best = 1e300;
    for (int t = 0; t < 7; ++t) { QueryPerformanceCounter(&a); for (int i = 0; i < N; ++i) sink += cspnA(s, set); QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)F.QuadPart / N; if (ns < best) best = ns; }
    return best;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    HMODULE s = LoadLibraryW(L"shlwapi.dll");
    cmpA = (PCMP)GetProcAddress(s, "ChrCmpIA"); chrA = (PCHR)GetProcAddress(s, "StrChrIA");
    rchrA = (PRCHR)GetProcAddress(s, "StrRChrIA"); cspnA = (PCSPN)GetProcAddress(s, "StrCSpnIA");
    chrW = (PCHRW)GetProcAddress(s, "StrChrIW");
    CPINFO ci; GetCPInfo(CP_ACP, &ci);
    printf("ACP %u, MaxCharSize %u, system LCID %04lX, user LCID %04lX\n", GetACP(), ci.MaxCharSize, GetSystemDefaultLCID(), GetUserDefaultLCID());

    /* 2/3. the relation, via StrChrIA on one-byte haystacks; and ChrCmpIA */
    long pairs = 0, asym = 0, cmp_diff = 0, cmp_diff_rev = 0, self_miss = 0;
    for (int n = 0; n < 256; ++n)
        for (int b = 1; b < 256; ++b) {
            char h[2] = { (char)b, 0 };
            rel[n][b] = chrA(h, (WORD)n) != NULL;
            pairs += rel[n][b];
            int c = cmpA((WORD)b, (WORD)n) == 0, c2 = cmpA((WORD)n, (WORD)b) == 0;
            if (c != rel[n][b]) ++cmp_diff;
            if (c2 != rel[n][b]) ++cmp_diff_rev;
        }
    for (int n = 1; n < 256; ++n) { if (!rel[n][n]) ++self_miss; for (int b = 1; b < 256; ++b) if (rel[n][b] != rel[b][n]) ++asym; }
    long intrans = 0;
    for (int a = 1; a < 256; ++a) for (int b = 1; b < 256; ++b) if (rel[a][b]) for (int c = 1; c < 256; ++c) if (rel[b][c] && !rel[a][c]) ++intrans;
    printf("2. matching (needle, byte) pairs: %ld of 65280; asymmetric %ld; needles not matching themselves %ld; intransitive triples %ld\n", pairs, asym, self_miss, intrans);
    printf("3. ChrCmpIA(b, n)==0 differs from StrChrIA on %ld pairs; ChrCmpIA(n, b) on %ld\n", cmp_diff, cmp_diff_rev);
    printf("   needle 0 matches:"); for (int b = 1; b < 256; ++b) if (rel[0][b]) printf(" %02X", b); printf("\n");
    printf("   needles matching more than their ASCII case pair:\n");
    for (int n = 1; n < 256; ++n) {
        int cnt = 0; for (int b = 1; b < 256; ++b) cnt += rel[n][b];
        int expect = ((n | 0x20) >= 'a' && (n | 0x20) <= 'z') ? 2 : 1;
        if (cnt != expect) { printf("     %02X (%d):", n, cnt); int k = 0; for (int b = 1; b < 256 && k < 24; ++b) if (rel[n][b]) { printf(" %02X", b); ++k; } printf("%s\n", cnt > 24 ? " ..." : ""); }
    }

    /* 4. the WORD needle's high byte */
    long hi = 0;
    for (int n = 0; n < 256; ++n) for (int b = 1; b < 256; b += 3) {
        char h[2] = { (char)b, 0 };
        for (int k = 1; k < 256; k += 37) if ((chrA(h, (WORD)(n | (k << 8))) != NULL) != rel[n][b]) ++hi;
    }
    printf("4. high byte of the WORD needle changes %ld answers\n", hi);

    /* 5. thread locale */
    LCID loc[4] = { 0x041F, 0x0411, 0x0401, 0x0419 };
    LCID old = GetThreadLocale(); LANGID oldui = GetThreadUILanguage();
    for (int l = 0; l < 4; ++l) {
        SetThreadLocale(loc[l]); SetThreadUILanguage(LANGIDFROMLCID(loc[l]));
        long d = 0;
        for (int n = 0; n < 256; ++n) for (int b = 1; b < 256; ++b) { char h[2] = { (char)b, 0 }; if ((chrA(h, (WORD)n) != NULL) != rel[n][b]) ++d; }
        printf("5. thread locale/UI %04lX: %ld answers differ\n", loc[l], d);
    }
    SetThreadLocale(old); SetThreadUILanguage(oldui);

    /* 6. shapes */
    const char* t = "abcdAbcd";
    printf("6. StrRChrIA(t, NULL, 'A') -> %td; (t, t+4, 'a') -> %td; (t, t+8, 'z') -> %p\n",
           rchrA(t, NULL, 'A') - t, rchrA(t, t + 4, 'a') - t, (void*)rchrA(t, t + 8, 'z'));
    /* A NUL inside [start, end): the export steps with CharNextA, which does not move at a NUL. Run it on
       a watchdog thread -- the first version of this probe called it directly and spun for ten minutes. */
    HANDLE th = CreateThread(NULL, 0, spin_probe, NULL, 0, NULL);
    DWORD w = WaitForSingleObject(th, 2000);
    if (w == WAIT_TIMEOUT) { printf("   StrRChrIA(\"ab\\0cdA\", +6, 'a'): DOES NOT RETURN after 2 s -- spinning at the NUL; thread terminated\n"); TerminateThread(th, 1); }
    else printf("   StrRChrIA(\"ab\\0cdA\", +6, 'a') returned %td\n", spin_result);
    CloseHandle(th);
    printf("   StrCSpnIA(\"hello World\", \"w\") -> %d; (\"abc\", \"\") -> %d; (\"\", \"x\") -> %d\n",
           cspnA("hello World", "w"), cspnA("abc", ""), cspnA("", "x"));

    /* 1. cost */
    SetThreadAffinityMask(GetCurrentThread(), 4); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    static char big[4097]; static wchar_t bigw[4097];
    memset(big, 'q', 4096); for (int i = 0; i < 4096; ++i) bigw[i] = L'q';
    printf("1. ns per call:\n");
    printf("   ChrCmpIA('a','A') %.2f   ('a','b') %.2f\n", time_cmpA('a', 'A', 20000), time_cmpA('a', 'b', 20000));
    int L[4] = { 1, 16, 256, 4096 };
    for (int k = 0; k < 4; ++k) {
        big[L[k]] = 0; bigw[L[k]] = 0;
        int N = L[k] >= 256 ? 200 : 5000;
        double a = time_chrA(big, 'z', N);
        printf("   miss over %4d: StrChrIA %10.1f ns (%.1f/char)  StrChrIW %9.1f  StrRChrIA(NULL end) %10.1f  StrCSpnIA(set \"xyz\") %10.1f\n",
               L[k], a, a / L[k], time_chrW(bigw, L'z', N), time_rchrA(big, NULL, 'z', N), time_cspnA(big, "xyz", N / 4 + 1));
        big[L[k]] = 'q'; bigw[L[k]] = L'q';
    }
    return 0;
}
