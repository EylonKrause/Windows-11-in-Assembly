// changes/248-urlunescapea/correctness.c
// THREE-WAY: ours, an independent oracle (reference.c), and the LIVE shlwapi!UrlUnescapeA export, on
// identical buffers, compared on the HRESULT, on *pcch, and on the whole buffer -- not just the
// destination. The whole buffer matters three times over here:
//
//   * the in-place path writes the SOURCE and never touches *pcch, so comparing only a destination
//     would compare nothing at all;
//   * a failed size test must leave the destination UNTOUCHED, which is only visible against a
//     poison fill;
//   * the overlap cases write inside the source, and the question is exactly which bytes.
//
// What the oracle is not asked. It cannot model a faulting source (it would have to fault to find
// out), so the unterminated-source case is tested against the LIVE EXPORT alone, at a PAGE_NOACCESS
// page, where the shipped function returns S_OK with an empty result and the wide form would crash.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define F_INPLACE     0x00100000u
#define F_AS_UTF8     0x00040000u
#define F_EXTRA_INFO  0x02000000u

extern HRESULT wia_urlunescapea(char*, char*, DWORD*, DWORD);
extern HRESULT ref_urlunescapea(char*, char*, DWORD*, DWORD);

typedef HRESULT (WINAPI *FN)(char*, char*, DWORD*, DWORD);
static FN sysfn;

#define BUFSZ 16384
#define SENT  0xAB

static char buf_ours[BUFSZ], buf_ref[BUFSZ], buf_sys[BUFSZ];

/* The compare window, not the whole buffer: sections 1-6 touch under a kilobyte and comparing 16 KB
   per case would spend all of the run's time in memset and memcmp rather than in the function. */
static int g_src = 64, g_dst = 512, g_win = 1024;

static long cases = 0, fails = 0;

typedef struct { HRESULT hr; DWORD cch; } R;

static void run_one(int which, const char* s, size_t n, int ds, int dh, DWORD cap, DWORD flags,
                    char* buf, R* r)
{
    char* dst;
    memset(buf, SENT, g_win);
    memcpy(buf + ds, s, n + 1);
    r->cch = cap;
    dst = (dh < 0) ? 0 : buf + dh;
    if (which == 0)      r->hr = wia_urlunescapea(buf + ds, dst, &r->cch, flags);
    else if (which == 1) r->hr = ref_urlunescapea(buf + ds, dst, &r->cch, flags);
    else                 r->hr = sysfn(buf + ds, dst, &r->cch, flags);
}

static void show(const char* s, size_t n)
{
    size_t i;
    printf("\"");
    for (i = 0; i < n && i < 48; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7F) printf("%c", c); else printf("\\x%02X", c);
    }
    if (n > 48) printf("...");
    printf("\"");
}

/* use_ref = 0 skips the oracle (the cases it cannot model); the live export is never skipped */
static int check(const char* s, size_t n, int ds, int dh, DWORD cap, DWORD flags, int use_ref)
{
    R a, b, c;
    int bad = 0;

    ++cases;
    run_one(0, s, n, ds, dh, cap, flags, buf_ours, &a);
    run_one(2, s, n, ds, dh, cap, flags, buf_sys,  &c);
    if (use_ref) run_one(1, s, n, ds, dh, cap, flags, buf_ref, &b);

    if (a.hr != c.hr || a.cch != c.cch || memcmp(buf_ours, buf_sys, g_win) != 0) bad = 1;
    if (use_ref && (a.hr != b.hr || a.cch != b.cch || memcmp(buf_ours, buf_ref, g_win) != 0)) bad = 2;

    if (bad) {
        if (++fails <= 25) {
            printf("  MISMATCH (%s) src=", bad == 1 ? "vs LIVE" : "vs ORACLE");
            show(s, n);
            printf(" ds=%d dh=%d cap=%lu flags=%08lX\n", ds, dh, (unsigned long)cap,
                   (unsigned long)flags);
            printf("    ours   hr=%08lX cch=%-6lu ", (unsigned long)a.hr, (unsigned long)a.cch);
            if (dh >= 0) show(buf_ours + dh, strnlen(buf_ours + dh, 48));
            printf(" src now "); show(buf_ours + ds, strnlen(buf_ours + ds, 48)); printf("\n");
            printf("    live   hr=%08lX cch=%-6lu ", (unsigned long)c.hr, (unsigned long)c.cch);
            if (dh >= 0) show(buf_sys + dh, strnlen(buf_sys + dh, 48));
            printf(" src now "); show(buf_sys + ds, strnlen(buf_sys + ds, 48)); printf("\n");
            if (use_ref) {
                printf("    oracle hr=%08lX cch=%-6lu ", (unsigned long)b.hr, (unsigned long)b.cch);
                if (dh >= 0) show(buf_ref + dh, strnlen(buf_ref + dh, 48));
                printf(" src now "); show(buf_ref + ds, strnlen(buf_ref + ds, 48)); printf("\n");
            }
            {   /* where, exactly */
                int i;
                for (i = 0; i < g_win; ++i)
                    if (buf_ours[i] != buf_sys[i]) {
                        printf("    first differing byte at +%d: ours %02X live %02X\n", i,
                               (unsigned char)buf_ours[i], (unsigned char)buf_sys[i]);
                        break;
                    }
            }
        }
        return 0;
    }
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/* '%' and three hex digits including '0' (so "%00" appears), a letter that is a hex digit, a letter
   that is not, and the two raw extra-info markers. Eight symbols, chosen so that every branch in
   impl.asm is reachable from a string six characters long. */
static const char ALPHA[] = "%410a?#z";


static void enumerate(int maxlen, void (*fn)(const char* s, size_t n))
{
    static char s[16];
    int na = (int)(sizeof ALPHA - 1);
    int len;
    for (len = 0; len <= maxlen; ++len) {
        long total = 1, k;
        int i;
        for (i = 0; i < len; ++i) total *= na;
        for (k = 0; k < total; ++k) {
            long v = k;
            for (i = 0; i < len; ++i) { s[i] = ALPHA[v % na]; v /= na; }
            s[len] = 0;
            fn(s, (size_t)len);
        }
    }
}

static void case_basic(const char* s, size_t n)
{
    check(s, n, g_src, g_dst, 64, 0, 1);
    check(s, n, g_src, g_dst, 64, F_EXTRA_INFO, 1);
}

static void case_caps(const char* s, size_t n)
{
    DWORD cap;
    for (cap = 1; cap <= (DWORD)n + 2; ++cap) {
        check(s, n, g_src, g_dst, cap, 0, 1);
        check(s, n, g_src, g_dst, cap, F_EXTRA_INFO, 1);
    }
}

static void case_inplace(const char* s, size_t n)
{
    check(s, n, g_src, -1, 0, F_INPLACE, 1);
    check(s, n, g_src, -1, 0, F_INPLACE | F_EXTRA_INFO, 1);
    check(s, n, g_src, -1, 64, F_INPLACE | F_AS_UTF8, 1);   /* INPLACE is tested FIRST */
}

static void case_overlap(const char* s, size_t n)
{
    int dd;
    for (dd = -8; dd <= 8; ++dd) {
        check(s, n, g_src, g_src + dd, 64, 0, 1);
        check(s, n, g_src, g_src + dd, 64, F_EXTRA_INFO, 1);
        check(s, n, g_src, g_src + dd, (DWORD)n, 0, 1);       /* and a tight buffer in the same place */
    }
}

static uint64_t rng_state = 0x243F6A8885A308D3ull;
static unsigned rng(void)
{
    rng_state = rng_state * 6364136223846793005ull + 1442695040888963407ull;
    return (unsigned)(rng_state >> 33);
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sysfn = (FN)GetProcAddress(h, "UrlUnescapeA");
    if (!sysfn) { printf("cannot resolve UrlUnescapeA\n"); return 1; }

    printf("UrlUnescapeA: ours vs an independent oracle vs the LIVE export\n\n");

    printf("1. enumerated over \"%s\" to length 6, flags 0 and DONT_UNESCAPE_EXTRA_INFO\n", ALPHA);
    enumerate(6, case_basic);
    printf("   %ld cases, %ld mismatches\n\n", cases, fails);

    {
        long mark = cases;
        printf("2. THE STRICT SIZE TEST: every capacity from 1 to len+2, to length 4\n");
        enumerate(4, case_caps);
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    {
        long mark = cases;
        printf("3. URL_UNESCAPE_INPLACE (dst NULL, *pcch 0) to length 5 -- where %%00 REFUSES\n");
        enumerate(5, case_inplace);
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    {
        long mark = cases;
        printf("4. OVERLAP: the destination at every offset -8..+8 from the source, to length 4\n");
        enumerate(4, case_overlap);
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    {   /* every possible escape pair, including the 65024 that are not escapes at all */
        long mark = cases;
        int hi, lo;
        char s[8];
        printf("5. ALL 65536 BYTE PAIRS as \"a%%XYb\" -- the hex set and the decoded value\n");
        for (hi = 0; hi < 256; ++hi)
            for (lo = 0; lo < 256; ++lo) {
                if (hi == 0 || lo == 0) continue;          /* a NUL there ends the string instead */
                s[0] = 'a'; s[1] = '%'; s[2] = (char)hi; s[3] = (char)lo; s[4] = 'b'; s[5] = 0;
                check(s, 5, g_src, g_dst, 64, 0, 1);
            }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    {   /* all 32 single flag bits, and pairs of the two that matter, on inputs that exercise each */
        long mark = cases;
        static const char* S[] = {
            "a%41b?c%42d#e%C3%A9f", "%00", "a%00b", "?%41", "#%41", "%3F%41", "%%41", "a%",
            "%2541", "%41%42%43%44", "plain", "", "%zz%41", "%4", "??##%41%42",
        };
        int i, b1, b2;
        printf("6. FLAG BITS: all 32 singly, and every pair of bits 18/20/25, on 15 inputs\n");
        for (i = 0; i < (int)(sizeof S / sizeof S[0]); ++i) {
            size_t n = strlen(S[i]);
            for (b1 = 0; b1 < 32; ++b1) {
                DWORD f = 1u << b1;
                if (f == F_INPLACE) check(S[i], n, g_src, -1, 0, f, 1);
                else                check(S[i], n, g_src, g_dst, 64, f, 1);
            }
            for (b1 = 0; b1 < 32; ++b1)
                for (b2 = 0; b2 < 32; ++b2) {
                    DWORD f = (1u << b1) | (1u << b2);
                    if (f & F_INPLACE) check(S[i], n, g_src, -1, 0, f, 1);
                    else               check(S[i], n, g_src, g_dst, 64, f, 1);
                }
        }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    {   /* long strings, at every escape density, and at lengths that straddle a 32-byte block */
        long mark = cases;
        static char s[5000];
        int dens, len, k;
        printf("7. LONG STRINGS: lengths 1..600 and 4000, escape density 0/6/25/50/100%%\n");
        g_dst = 8192; g_win = BUFSZ;            /* now the whole buffer is in play */
        for (dens = 0; dens <= 4; ++dens) {
            static const int PCT[5] = { 0, 6, 25, 50, 100 };
            for (len = 1; len <= 600; ++len) {
                for (k = 0; k < len; ++k) {
                    if ((int)(rng() % 100) < PCT[dens] && k + 2 < len) {
                        s[k] = '%'; s[k+1] = "0123456789ABCDEFabcdefxyz?#%"[rng() % 28];
                        s[k+2] = "0123456789ABCDEFabcdefxyz?#%"[rng() % 28];
                        k += 2;
                    } else {
                        s[k] = (char)(0x20 + (rng() % 95));
                    }
                }
                s[len] = 0;
                check(s, strlen(s), g_src, g_dst, 4096, 0, 1);
                check(s, strlen(s), g_src, g_dst, 4096, F_EXTRA_INFO, 1);
                check(s, strlen(s), g_src, -1, 0, F_INPLACE, 1);
            }
            for (k = 0; k < 4000; ++k)
                s[k] = ((int)(rng() % 100) < PCT[dens]) ? "%41%42%2541xy"[rng() % 13]
                                                        : (char)(0x20 + (rng() % 95));
            s[4000] = 0;
            check(s, 4000, g_src, g_dst, 4096, 0, 1);
            check(s, 4000, g_src, g_dst, 4096, F_EXTRA_INFO, 1);
        }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    {   /* and the same long strings at a capacity one short of the answer */
        long mark = cases;
        static char s[1200];
        int len, k;
        printf("8. LONG STRINGS AT A CAPACITY ONE SHORT of the result, lengths 1..400\n");
        for (len = 1; len <= 400; ++len) {
            for (k = 0; k < len; ++k)
                s[k] = ((int)(rng() % 100) < 30) ? "%4102aFz"[rng() % 8]
                                                 : (char)(0x20 + (rng() % 95));
            s[len] = 0;
            {   /* ask ours for the needed size, then feed exactly that and one less */
                DWORD need = 1;
                char probe[8];
                R r;
                run_one(0, s, strlen(s), g_src, g_dst, 1, 0, buf_ours, &r);
                need = r.cch;                    /* len+1 when it refused */
                (void)probe;
                if (need >= 1) {
                    check(s, strlen(s), g_src, g_dst, need, 0, 1);
                    check(s, strlen(s), g_src, g_dst, need - 1, 0, 1);
                    if (need >= 2) check(s, strlen(s), g_src, g_dst, need - 2, 0, 1);
                }
            }
        }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    {   /* NULL and zero arguments: all four measured at E_INVALIDARG */
        DWORD cch;
        HRESULT a, b, c;
        int i;
        struct { const char* name; int src_null, dst_null, pcch_null; DWORD cap; } T[] = {
            { "src NULL",  1, 0, 0, 64 }, { "dst NULL",  0, 1, 0, 64 },
            { "pcch NULL", 0, 0, 1, 64 }, { "*pcch 0",   0, 0, 0, 0  },
        };
        printf("9. NULL AND ZERO ARGUMENTS\n");
        for (i = 0; i < 4; ++i) {
            char s[16]; DWORD* p;
            strcpy(s, "a%41b");
            cch = T[i].cap; p = T[i].pcch_null ? 0 : &cch;
            a = wia_urlunescapea(T[i].src_null ? 0 : s, T[i].dst_null ? 0 : buf_ours, p, 0);
            cch = T[i].cap; p = T[i].pcch_null ? 0 : &cch;
            b = ref_urlunescapea(T[i].src_null ? 0 : s, T[i].dst_null ? 0 : buf_ref, p, 0);
            cch = T[i].cap; p = T[i].pcch_null ? 0 : &cch;
            c = sysfn(T[i].src_null ? 0 : s, T[i].dst_null ? 0 : buf_sys, p, 0);
            ++cases;
            printf("   %-10s ours %08lX  oracle %08lX  live %08lX %s\n", T[i].name,
                   (unsigned long)a, (unsigned long)b, (unsigned long)c,
                   (a == b && b == c) ? "" : "<== MISMATCH");
            if (!(a == b && b == c)) ++fails;
        }
        printf("\n");
    }

    {   /* THE ASYMMETRY THE ORACLE CANNOT MODEL: a faulting source, against the live export only */
        SYSTEM_INFO si; GetSystemInfo(&si);
        {
            SIZE_T pg = si.dwPageSize;
            char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            DWORD old, cch;
            int tail, bad = 0;
            printf("10. A FAULTING SOURCE at a PAGE_NOACCESS page -- lstrlenA SWALLOWS it, so this\n"
                   "    returns S_OK with an empty result where the WIDE form faults\n");
            if (g) {
                VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
                for (tail = 1; tail <= 40; ++tail) {
                    char* s = (g + pg) - tail;
                    HRESULT ha = 0, hc = 0;
                    DWORD ca = 0, cc = 0;
                    int fa = 0, fc = 0, i;
                    for (i = 0; i < tail; ++i) s[i] = (char)('a' + (i % 26));  /* NO terminator */
                    memset(buf_ours, SENT, 128); memset(buf_sys, SENT, 128);
                    cch = 64;
                    __try { ha = wia_urlunescapea(s, buf_ours, &cch, 0); ca = cch; }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
                    cch = 64;
                    __try { hc = sysfn(s, buf_sys, &cch, 0); cc = cch; }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
                    ++cases;
                    if (fa != fc || ha != hc || ca != cc || memcmp(buf_ours, buf_sys, 128) != 0) {
                        ++fails; ++bad;
                        if (bad <= 6)
                            printf("    tail=%d ours %s %08lX/%lu   live %s %08lX/%lu\n", tail,
                                   fa ? "FAULTED" : "ok", (unsigned long)ha, (unsigned long)ca,
                                   fc ? "FAULTED" : "ok", (unsigned long)hc, (unsigned long)cc);
                    }
                }
                /* and a source TERMINATED at the last readable byte must not fault either way */
                for (tail = 2; tail <= 80; ++tail) {
                    char* s = (g + pg) - tail;
                    int i, fa = 0, fc = 0;
                    HRESULT ha = 0, hc = 0;
                    DWORD ca = 0, cc = 0;
                    for (i = 0; i < tail - 1; ++i)
                        s[i] = (i % 3 == 0) ? '%' : (i % 3 == 1) ? '4' : '1';
                    s[tail - 1] = 0;
                    memset(buf_ours, SENT, 128); memset(buf_sys, SENT, 128);
                    cch = 64;
                    __try { ha = wia_urlunescapea(s, buf_ours, &cch, 0); ca = cch; }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
                    cch = 64;
                    __try { hc = sysfn(s, buf_sys, &cch, 0); cc = cch; }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
                    ++cases;
                    if (fa || fc || ha != hc || ca != cc || memcmp(buf_ours, buf_sys, 128) != 0) {
                        ++fails; ++bad;
                        if (bad <= 10)
                            printf("    TERMINATED tail=%d ours %s %08lX/%lu  live %s %08lX/%lu\n",
                                   tail, fa ? "FAULTED" : "ok", (unsigned long)ha,
                                   (unsigned long)ca, fc ? "FAULTED" : "ok", (unsigned long)hc,
                                   (unsigned long)cc);
                    }
                }
                printf("    40 unterminated tails + 79 terminated-at-the-last-byte: %s\n\n",
                       bad ? "MISMATCHES ABOVE" : "identical to the live export");
                VirtualFree(g, 0, MEM_RELEASE);
            }
        }
    }

    printf("%ld cases, %ld mismatches -- %s\n", cases, fails,
           fails ? "CORRECTNESS FAILED" : "bit-exact");
    return fails ? 1 : 0;
}
