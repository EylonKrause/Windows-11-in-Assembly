/* changes/249-urlhasha/correctness.c
 *
 * THREE-WAY: ours, an independent oracle (reference.c, whose permutation table is recovered at
 * runtime from the live HashData export rather than carried as a constant), and the LIVE
 * shlwapi!UrlHashA -- compared on the HRESULT and on the whole buffer against a poison fill.
 *
 * The whole buffer, not the first cbHash bytes, for two reasons measured before this was written:
 * cbHash is not validated anywhere in the shipped envelope, so "writes nothing past cbHash" is a
 * property to test rather than assume; and the overlap cases write INSIDE the URL, where the
 * question is exactly which bytes.
 *
 * a fourth comparison that is the point of the change: every disjoint case is also checked against
 * the live HashData export on the same bytes. UrlHashA is supposed to BE HashData behind lstrlenA,
 * and that equivalence is what lets this change reuse change 244's kernel instead of re-deriving a
 * measured algorithm. If it ever stops holding, this harness says so rather than the change quietly
 * hashing the wrong thing.
 *
 * AND A FIFTH: UrlHashW is compared against UrlHashA on the same ASCII text, because the wide
 * export is a converter that CALLS the narrow one -- so a patch on the narrow one is a patch on
 * both, and the live-substitution harness relies on that being true.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern HRESULT wia_urlhasha(const char*, BYTE*, DWORD);
extern HRESULT ref_urlhasha(const char*, BYTE*, DWORD);
extern int ref_init(void);
extern const unsigned char* ref_table(void);

typedef HRESULT (WINAPI *FA)(const char*, BYTE*, DWORD);
typedef HRESULT (WINAPI *FW)(const wchar_t*, BYTE*, DWORD);
typedef HRESULT (WINAPI *FH)(const BYTE*, DWORD, BYTE*, DWORD);
static FA sysa;
static FW sysw;
static FH sysh;

#define BUFSZ 2048
#define SENT  0xAB

static BYTE b_ours[BUFSZ], b_ref[BUFSZ], b_sys[BUFSZ], b_hd[BUFSZ];
static long cases = 0, fails = 0;

static void show(const BYTE* p, DWORD n)
{
    DWORD i;
    for (i = 0; i < n && i < 20; ++i) printf("%02X", p[i]);
    if (n > 20) printf("..");
}

/* One case: the URL is placed at offset `us` in each buffer and the digest at `ds`; when they
   overlap, the URL is written first and the digest pointer lands inside it, exactly as a caller
   sharing one buffer would produce. hd = 1 also compares the live HashData export. */
static void check(const char* url, size_t n, int us, int ds, DWORD cb, int want_hd)
{
    HRESULT ra, rb, rc;
    int bad = 0;

    ++cases;
    memset(b_ours, SENT, BUFSZ); memcpy(b_ours + us, url, n + 1);
    memset(b_ref,  SENT, BUFSZ); memcpy(b_ref  + us, url, n + 1);
    memset(b_sys,  SENT, BUFSZ); memcpy(b_sys  + us, url, n + 1);

    ra = wia_urlhasha((const char*)(b_ours + us), b_ours + ds, cb);
    rb = ref_urlhasha((const char*)(b_ref  + us), b_ref  + ds, cb);
    rc = sysa((const char*)(b_sys + us), b_sys + ds, cb);

    if (ra != rc || memcmp(b_ours, b_sys, BUFSZ) != 0) bad = 1;
    if (ra != rb || memcmp(b_ours, b_ref, BUFSZ) != 0) bad = 2;

    if (want_hd) {
        HRESULT rd;
        memset(b_hd, SENT, BUFSZ); memcpy(b_hd + us, url, n + 1);
        rd = sysh((const BYTE*)(b_hd + us), (DWORD)n, b_hd + ds, cb);
        if (rd != S_OK || memcmp(b_ours, b_hd, BUFSZ) != 0) bad = 3;
    }

    if (bad) {
        if (++fails <= 20) {
            static const char* W[4] = { "", "vs LIVE UrlHashA", "vs ORACLE", "vs LIVE HashData" };
            int i;
            printf("  MISMATCH (%s) url=\"%.40s\" len=%llu us=%d ds=%d cb=%lu\n", W[bad], url,
                   (unsigned long long)n, us, ds, (unsigned long)cb);
            printf("    ours   %08lX ", (unsigned long)ra); show(b_ours + ds, cb ? cb : 4);
            printf("\n    live   %08lX ", (unsigned long)rc); show(b_sys + ds, cb ? cb : 4);
            printf("\n    oracle %08lX ", (unsigned long)rb); show(b_ref + ds, cb ? cb : 4);
            if (bad == 3) { printf("\n    HashData        "); show(b_hd + ds, cb ? cb : 4); }
            printf("\n");
            for (i = 0; i < BUFSZ; ++i)
                if (b_ours[i] != b_sys[i]) {
                    printf("    first differing byte at +%d: ours %02X live %02X\n", i,
                           b_ours[i], b_sys[i]);
                    break;
                }
        }
    }
}

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static unsigned rng(void){ rs = rs * 6364136223846793005ull + 1442695040888963407ull;
                           return (unsigned)(rs >> 33); }

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    int distinct;
    static char url[1200];

    sysa = (FA)GetProcAddress(h, "UrlHashA");
    sysw = (FW)GetProcAddress(h, "UrlHashW");
    sysh = (FH)GetProcAddress(h, "HashData");
    if (!sysa || !sysw || !sysh) { printf("cannot resolve UrlHashA/W or HashData\n"); return 1; }

    printf("UrlHashA: ours vs an independent oracle vs the LIVE export\n\n");

    distinct = ref_init();
    printf("0. THE ORACLE'S TABLE, recovered at runtime by 256 one-byte calls to the live\n"
           "   HashData export -- a DIFFERENT export from the one under test, whose worker the\n"
           "   disassembly says shares the table at RVA 0x2A6010\n");
    printf("   %d distinct entries of 256  %s\n\n", distinct,
           distinct == 256 ? "(a permutation, as change 244 measured)" : "<== NOT A PERMUTATION");
    if (distinct != 256) { printf("CORRECTNESS FAILED (table)\n"); return 1; }

    /* ---- 1. every cbHash from 0 to 300, against several URL lengths ---------------------- */
    {
        long mark = cases;
        int L[] = { 0, 1, 2, 3, 4, 5, 7, 8, 11, 12, 13, 16, 23, 24, 25, 31, 32, 33, 64, 100 };
        int i;
        DWORD cb;
        printf("1. EVERY cbHash FROM 0 TO 300 against 20 url lengths -- the seed WRAPS at 256,\n"
               "   and change 244 switches kernel at cbHash 5 and again every 12 lanes\n");
        for (i = 0; i < 20; ++i) {
            int k;
            for (k = 0; k < L[i]; ++k) url[k] = (char)('a' + (k * 7) % 26);
            url[L[i]] = 0;
            for (cb = 0; cb <= 300; ++cb) check(url, L[i], 64, 1024, cb, 1);
        }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    /* ---- 2. every single-byte url, and every two-byte url ------------------------------- */
    {
        long mark = cases;
        int a, b;
        printf("2. ALL 255 ONE-BYTE AND ALL 65025 TWO-BYTE URLS at cbHash 4 -- the sweep that\n"
               "   fixed the CONSUMPTION ORDER for change 244 (last byte first matches all of\n"
               "   them; first byte first matches only the 255 palindromes)\n");
        for (a = 1; a < 256; ++a) {
            url[0] = (char)a; url[1] = 0;
            check(url, 1, 64, 1024, 4, 1);
        }
        for (a = 1; a < 256; ++a)
            for (b = 1; b < 256; ++b) {
                url[0] = (char)a; url[1] = (char)b; url[2] = 0;
                check(url, 2, 64, 1024, 4, 1);
            }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    /* ---- 3. OVERLAP: the digest at every offset relative to the url --------------------- */
    {
        long mark = cases;
        int doff, i;
        DWORD cb;
        printf("3. OVERLAP: the digest at every offset -24..+24 from a 24-byte url, at seven\n"
               "   digest sizes. Change 244's grouped kernel is WRONG on all 1641 overlapping\n"
               "   placements its own probe enumerated and hands them to a byte-for-byte\n"
               "   fallback -- and UrlHashA passes the caller's pointers straight through, so\n"
               "   that fallback has to be reachable through this envelope too.\n");
        for (i = 0; i < 24; ++i) url[i] = (char)('a' + i);
        url[24] = 0;
        for (doff = -24; doff <= 24; ++doff) {
            static const DWORD CB[] = { 1, 2, 4, 5, 12, 13, 20 };
            int c;
            for (c = 0; c < 7; ++c) {
                cb = CB[c];
                /* want_hd = 0: HashData takes an explicit length, so on an overlapping placement
                   the two are not the same question -- the seed can move the URL's terminator. */
                check(url, 24, 64, 64 + doff, cb, 0);
            }
        }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    /* ---- 4. long urls, and urls straddling every 32-byte boundary ----------------------- */
    {
        long mark = cases;
        int n, k;
        printf("4. URL LENGTHS 0..600 (which walks change 225's scan across every block\n"
               "   boundary) plus five 1000-byte urls of random bytes\n");
        for (n = 0; n <= 600; ++n) {
            for (k = 0; k < n; ++k) url[k] = (char)(1 + (rng() % 255));
            url[n] = 0;
            check(url, n, 64, 1024, 16, 1);
        }
        for (k = 0; k < 5; ++k) {
            int j;
            for (j = 0; j < 1000; ++j) url[j] = (char)(1 + (rng() % 255));
            url[1000] = 0;
            check(url, 1000, 8, 1024, (DWORD)(1 + k * 7), 1);
        }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    /* ---- 5. the url at every alignment, so the length scan is exercised off-page-edge --- */
    {
        long mark = cases;
        int off, n;
        printf("5. THE URL AT EVERY ALIGNMENT 0..63, at three lengths -- change 225's scan\n"
               "   aligns its first load DOWN, so its masking is alignment-dependent\n");
        for (off = 0; off < 64; ++off)
            for (n = 0; n <= 70; n += 7) {
                int k;
                for (k = 0; k < n; ++k) url[k] = (char)('a' + k % 26);
                url[n] = 0;
                check(url, n, 64 + off, 1024, 16, 1);
            }
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, fails);
    }

    /* ---- 6. NULL arguments -------------------------------------------------------------- */
    {
        HRESULT a, b, c;
        int i;
        printf("6. NULL ARGUMENTS\n");
        for (i = 0; i < 3; ++i) {
            const char* u = (i == 0) ? 0 : "abc";
            BYTE* d1 = (i == 1) ? 0 : b_ours;
            BYTE* d2 = (i == 1) ? 0 : b_ref;
            BYTE* d3 = (i == 1) ? 0 : b_sys;
            DWORD cb = (i == 2) ? 0 : 16;
            memset(b_ours, SENT, 64); memset(b_ref, SENT, 64); memset(b_sys, SENT, 64);
            a = wia_urlhasha(u, d1, cb);
            b = ref_urlhasha(u, d2, cb);
            c = sysa(u, d3, cb);
            ++cases;
            printf("   %-12s ours %08lX  oracle %08lX  live %08lX  %s\n",
                   i == 0 ? "url NULL" : i == 1 ? "hash NULL" : "cbHash 0",
                   (unsigned long)a, (unsigned long)b, (unsigned long)c,
                   (a == b && b == c && (i == 1 || memcmp(b_ours, b_sys, 64) == 0))
                       ? "" : "<== MISMATCH");
            if (!(a == b && b == c && (i == 1 || memcmp(b_ours, b_sys, 64) == 0))) ++fails;
        }
        printf("\n");
    }

    /* ---- 7. UrlHashW is UrlHashA on the converted text ----------------------------------- */
    {
        long mark = cases, bad = 0;
        int i;
        DWORD cb;
        printf("7. UrlHashW(L\"x\") == ours(\"x\") for ASCII -- the wide export is a converter\n"
               "   that CALLS the narrow one (0x12F81F: call 0x12F750), which is why patching\n"
               "   the narrow export covers both\n");
        for (i = 0; i <= 120; ++i) {
            wchar_t w[300];
            int k;
            for (k = 0; k < i; ++k) { url[k] = (char)('a' + (k * 5) % 26); w[k] = (wchar_t)url[k]; }
            url[i] = 0; w[i] = 0;
            for (cb = 0; cb <= 20; ++cb) {
                memset(b_ours, SENT, BUFSZ); memset(b_sys, SENT, BUFSZ);
                if (wia_urlhasha(url, b_ours, cb) != sysw(w, b_sys, cb)) ++bad;
                if (memcmp(b_ours, b_sys, BUFSZ) != 0) ++bad;
                ++cases;
            }
        }
        fails += bad;
        printf("   %ld cases, %ld mismatches\n\n", cases - mark, bad);
    }

    /* ---- 8. A FAULTING URL, against the live export alone -------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        {
            SIZE_T pg = si.dwPageSize;
            char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            DWORD old;
            int tail, bad = 0;
            printf("8. A FAULTING URL at a PAGE_NOACCESS page. lstrlenA is SEH-wrapped, so the\n"
                   "   shipped function hashes NOTHING and leaves the identity seed. The oracle\n"
                   "   cannot model that -- it would have to fault to find the length -- so this\n"
                   "   runs against the live export alone. An implementation that called a bare\n"
                   "   scan instead of change 225's wrapper would CRASH here.\n");
            if (g) {
                VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
                for (tail = 1; tail <= 48; ++tail) {
                    char* s = (g + pg) - tail;
                    HRESULT ra = 0, rc = 0;
                    int fa = 0, fc = 0, i;
                    for (i = 0; i < tail; ++i) s[i] = (char)('a' + i % 26);   /* NO terminator */
                    memset(b_ours, SENT, 512); memset(b_sys, SENT, 512);
                    __try { ra = wia_urlhasha(s, b_ours, 16); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
                    __try { rc = sysa(s, b_sys, 16); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
                    ++cases;
                    if (fa != fc || ra != rc || memcmp(b_ours, b_sys, 512) != 0) {
                        ++fails; ++bad;
                        if (bad <= 6)
                            printf("    tail=%d ours %s %08lX   live %s %08lX\n", tail,
                                   fa ? "FAULTED" : "ok", (unsigned long)ra,
                                   fc ? "FAULTED" : "ok", (unsigned long)rc);
                    }
                }
                /* and a url terminated exactly at the last readable byte must not fault at all */
                for (tail = 2; tail <= 80; ++tail) {
                    char* s = (g + pg) - tail;
                    HRESULT ra = 0, rc = 0;
                    int fa = 0, fc = 0, i;
                    for (i = 0; i < tail - 1; ++i) s[i] = (char)('a' + i % 26);
                    s[tail - 1] = 0;
                    memset(b_ours, SENT, 512); memset(b_sys, SENT, 512);
                    __try { ra = wia_urlhasha(s, b_ours, 16); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
                    __try { rc = sysa(s, b_sys, 16); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
                    ++cases;
                    if (fa || fc || ra != rc || memcmp(b_ours, b_sys, 512) != 0) {
                        ++fails; ++bad;
                        if (bad <= 10)
                            printf("    TERMINATED tail=%d ours %s  live %s\n", tail,
                                   fa ? "FAULTED" : "ok", fc ? "FAULTED" : "ok");
                    }
                }
                printf("    48 unterminated tails + 79 terminated at the last readable byte: %s\n\n",
                       bad ? "MISMATCHES ABOVE" : "identical to the live export");
                VirtualFree(g, 0, MEM_RELEASE);
            }
        }
    }

    printf("%ld cases, %ld mismatches -- %s\n", cases, fails,
           fails ? "CORRECTNESS FAILED" : "bit-exact");
    return fails ? 1 : 0;
}
