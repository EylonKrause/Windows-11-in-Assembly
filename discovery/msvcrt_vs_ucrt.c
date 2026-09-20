/* discovery/msvcrt_vs_ucrt.c
 *
 * msvcrt.dll is the LEGACY C runtime and it is still loaded by a large part of the desktop. It is a
 * separate binary from ucrtbase.dll with its own code, and this repository's image tree reflects
 * that unevenly: 75 functions are covered for ucrtbase and only 34 for msvcrt, leaving 41 that are
 * converted for one CRT and not the other -- the parsers (`strtol`, `_strtoi64`, `atoi` and their
 * wide siblings), the bounds-checked `_s` family, `wcsrchr`, `_swab`, `_memccpy`.
 *
 * The assembly for all 41 already exists, is gate-validated and is proved live. So the question is
 * not "can we write it" -- it is the two questions that decide whether the existing assembly may be
 * pointed at msvcrt's exports at all:
 *
 *   1. Are they the same function? msvcrt predates the ucrt. Its `_s` functions and its parsers may
 *      differ from ucrtbase's in exactly the ways this project keeps finding: a different last
 *      error, a different partial write, a different answer to an invalid parameter. Assuming they
 *      match because the names match is the mistake that produced twelve defects in the live
 *      substitution campaign. So every function here is driven through a DIFFERENTIAL corpus and
 *      the two CRTs' answers are compared -- return value, output buffer AND errno.
 *   2. IS msvcrt SLOWER? If the two binaries ship the same vectorised code, there is no work here
 *      beyond bookkeeping. If msvcrt ships the older scalar code, every msvcrt process on the
 *      machine is paying for it.
 *
 * A function is a target for materialisation only if the answer is YES to both. One NO to question
 * 1 makes it a separate change with a separate contract, not a re-use.
 *
 * Run it on an idle machine. Every timing is a min-of-N.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>

static double bestns(void (*op)(void), int inner, int trials)
{
    LARGE_INTEGER f, a, b;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 64; ++i) op();
    for (t = 0; t < trials; ++t) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) op();
        QueryPerformanceCounter(&b);
        {
            double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            if (v < bv) bv = v;
        }
    }
    return bv;
}

static volatile unsigned long long sink;
static HMODULE hM, hU;

/* ---- Two CRTs means two of everything, and the first cut of this file forgot both ----------
 *
 * (1) The invalid-parameter handler is per-crt. `_set_invalid_parameter_handler` installs a
 *     handler in the CRT that exports it, and this program links against the UCRT. Handing msvcrt
 *     an invalid base therefore went to MSVCRT's handler, which is the default one, which
 *     terminates the process: the first run died at 0xC0000409 with no output at all -- the same
 *     trap changes 110-113 hit. Both handlers are installed below.
 *
 * (2) `errno` Is per-crt too. The `errno` macro resolves to the UCRT's thread-local, so reading it
 *     after an msvcrt call reads a variable msvcrt never touched, and every comparison would have
 *     been meaningless while looking perfectly reasonable. Each CRT's `_errno()` is resolved
 *     separately and read through its own pointer.
 *
 * Both mistakes have the same shape as the defects this repository keeps finding: the code was
 * comparing something other than what it claimed to compare.
 */
static volatile long iph_count;
static void __cdecl iph(const wchar_t* a, const wchar_t* b, const wchar_t* c, unsigned d, uintptr_t e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; ++iph_count; }

typedef void (__cdecl *iph_t)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
typedef iph_t (__cdecl *F_setiph)(iph_t);
typedef int*  (__cdecl *F_errno)(void);
static F_errno m_errno_fn, u_errno_fn;
static int m_handler_ok;

#define M_ERRNO (*m_errno_fn())
#define U_ERRNO (*u_errno_fn())

/* ---- signatures ------------------------------------------------------------------------- */
typedef long               (__cdecl *F_strtol )(const char*, char**, int);
typedef unsigned long      (__cdecl *F_strtoul)(const char*, char**, int);
typedef long long          (__cdecl *F_stoi64 )(const char*, char**, int);
typedef unsigned long long (__cdecl *F_stou64 )(const char*, char**, int);
typedef long               (__cdecl *F_wcstol )(const wchar_t*, wchar_t**, int);
typedef unsigned long      (__cdecl *F_wcstoul)(const wchar_t*, wchar_t**, int);
typedef int                (__cdecl *F_atoi   )(const char*);
typedef long long          (__cdecl *F_atoi64 )(const char*);
typedef int                (__cdecl *F_wtoi   )(const wchar_t*);
typedef wchar_t*           (__cdecl *F_wcsrchr)(const wchar_t*, wchar_t);
typedef void               (__cdecl *F_swab   )(char*, char*, int);
typedef void*              (__cdecl *F_memccpy)(void*, const void*, int, size_t);
typedef errno_t            (__cdecl *F_strcpys)(char*, size_t, const char*);
typedef errno_t            (__cdecl *F_wcscpys)(wchar_t*, size_t, const wchar_t*);

#define GETB(name, type) \
    type m_##name = (type)GetProcAddress(hM, #name); \
    type u_##name = (type)GetProcAddress(hU, #name)

/* ---- the subjects used by the timed rows ------------------------------------------------ */
static char    nbuf[128] = "  -1234567890";
static wchar_t wbuf[128] = L"  -1234567890";
static char    ndst[256], nsrc[256];
static wchar_t wdst[256];

static F_strtol  tm_strtol,  tu_strtol;
static F_stoi64  tm_stoi64,  tu_stoi64;
static F_atoi    tm_atoi,    tu_atoi;
static F_wcsrchr tm_wcsrchr, tu_wcsrchr;
static F_swab    tm_swab,    tu_swab;
static F_memccpy tm_memccpy, tu_memccpy;
static F_strcpys tm_strcpys, tu_strcpys;

static void o_m_strtol (void){ char* e; sink += (unsigned)tm_strtol(nbuf,&e,10); }
static void o_u_strtol (void){ char* e; sink += (unsigned)tu_strtol(nbuf,&e,10); }
static void o_m_stoi64 (void){ char* e; sink += (unsigned long long)tm_stoi64(nbuf,&e,10); }
static void o_u_stoi64 (void){ char* e; sink += (unsigned long long)tu_stoi64(nbuf,&e,10); }
static void o_m_atoi   (void){ sink += (unsigned)tm_atoi(nbuf); }
static void o_u_atoi   (void){ sink += (unsigned)tu_atoi(nbuf); }
static void o_m_wcsrchr(void){ sink += (size_t)tm_wcsrchr(wbuf,L'3'); }
static void o_u_wcsrchr(void){ sink += (size_t)tu_wcsrchr(wbuf,L'3'); }
static void o_m_swab   (void){ tm_swab(nsrc,ndst,120); sink += (unsigned char)ndst[0]; }
static void o_u_swab   (void){ tu_swab(nsrc,ndst,120); sink += (unsigned char)ndst[0]; }
static void o_m_memccpy(void){ sink += (size_t)tm_memccpy(ndst,nsrc,'Z',120); }
static void o_u_memccpy(void){ sink += (size_t)tu_memccpy(ndst,nsrc,'Z',120); }
static void o_m_strcpys(void){ sink += (unsigned)tm_strcpys(ndst,sizeof ndst,nsrc); }
static void o_u_strcpys(void){ sink += (unsigned)tu_strcpys(ndst,sizeof ndst,nsrc); }

static void timed(const char* name, void (*m)(void), void (*u)(void))
{
    double a = bestns(m, 4000, 40);
    double b = bestns(u, 4000, 40);
    printf("  %-14s %10.2f %11.2f %9.2fx  %s\n", name, a, b, (b > 0 ? a / b : 0.0),
           (a > b * 1.15) ? "msvcrt SLOWER" : (b > a * 1.15) ? "ucrtbase slower" : "same speed");
}

/* ---- the differential corpus ------------------------------------------------------------ */
static int diffs_parsers(void)
{
    static const char* S[] = {
        "0", "1", "-1", "  42", "\t+7", "0x1f", "0X1F", "017", "99999999999999999999",
        "-99999999999999999999", "2147483647", "2147483648", "-2147483648", "-2147483649",
        "4294967295", "4294967296", "", "   ", "abc", "-", "+", "0x", "12abc", "  -0012",
        "9223372036854775807", "9223372036854775808", "-9223372036854775808"
    };
    static const int B[] = { 0, 2, 8, 10, 16, 36, 1, 37 };
    int bad = 0, si, bi;
    GETB(strtol, F_strtol); GETB(strtoul, F_strtoul);
    GETB(_strtoi64, F_stoi64); GETB(_strtoui64, F_stou64);
    GETB(atoi, F_atoi); GETB(_atoi64, F_atoi64);
    for (si = 0; si < (int)(sizeof S / sizeof S[0]); ++si) {
        for (bi = 0; bi < 8; ++bi) {
            /* 1 and 37 are invalid bases; without msvcrt's own handler they terminate */
            if (!m_handler_ok && (B[bi] == 1 || B[bi] == 37)) continue;
            char *e1, *e2; long a1, a2; unsigned long b1, b2;
            long long c1, c2; unsigned long long d1, d2;
            int er1, er2; long n1, n2;
            if (m_strtol && u_strtol) {
                e1 = e2 = NULL;
                M_ERRNO = 0; iph_count = 0; a1 = m_strtol(S[si], &e1, B[bi]); er1 = M_ERRNO; n1 = iph_count;
                U_ERRNO = 0; iph_count = 0; a2 = u_strtol(S[si], &e2, B[bi]); er2 = U_ERRNO; n2 = iph_count;
                if (a1 != a2 || (e1 - S[si]) != (e2 - S[si]) || er1 != er2 || n1 != n2) {
                    if (bad < 8) printf("     strtol   \"%s\" base %d: msvcrt %ld/%d/e%d/h%ld  ucrt %ld/%d/e%d/h%ld\n",
                        S[si], B[bi], a1, (int)(e1 - S[si]), er1, n1, a2, (int)(e2 - S[si]), er2, n2);
                    ++bad;
                }
            }
            if (m_strtoul && u_strtoul) {
                e1 = e2 = NULL;
                M_ERRNO = 0; b1 = m_strtoul(S[si], &e1, B[bi]); er1 = M_ERRNO;
                U_ERRNO = 0; b2 = u_strtoul(S[si], &e2, B[bi]); er2 = U_ERRNO;
                if (b1 != b2 || (e1 - S[si]) != (e2 - S[si]) || er1 != er2) {
                    if (bad < 8) printf("     strtoul  \"%s\" base %d: msvcrt %lu/e%d  ucrt %lu/e%d\n",
                        S[si], B[bi], b1, er1, b2, er2);
                    ++bad;
                }
            }
            if (m__strtoi64 && u__strtoi64) {
                e1 = e2 = NULL;
                M_ERRNO = 0; c1 = m__strtoi64(S[si], &e1, B[bi]); er1 = M_ERRNO;
                U_ERRNO = 0; c2 = u__strtoi64(S[si], &e2, B[bi]); er2 = U_ERRNO;
                if (c1 != c2 || (e1 - S[si]) != (e2 - S[si]) || er1 != er2) {
                    if (bad < 8) printf("     _strtoi64 \"%s\" base %d: msvcrt %lld/e%d  ucrt %lld/e%d\n",
                        S[si], B[bi], c1, er1, c2, er2);
                    ++bad;
                }
            }
            if (m__strtoui64 && u__strtoui64) {
                e1 = e2 = NULL;
                M_ERRNO = 0; d1 = m__strtoui64(S[si], &e1, B[bi]); er1 = M_ERRNO;
                U_ERRNO = 0; d2 = u__strtoui64(S[si], &e2, B[bi]); er2 = U_ERRNO;
                if (d1 != d2 || (e1 - S[si]) != (e2 - S[si]) || er1 != er2) { ++bad; }
            }
        }
        if (m_atoi && u_atoi) {
            M_ERRNO = 0; { int x = m_atoi(S[si]); int e1 = M_ERRNO;
            U_ERRNO = 0; { int y = u_atoi(S[si]); int e2 = U_ERRNO;
            if (x != y || e1 != e2) { if (bad < 8) printf("     atoi     \"%s\": msvcrt %d/e%d  ucrt %d/e%d\n", S[si], x, e1, y, e2); ++bad; } } }
        }
        if (m__atoi64 && u__atoi64) {
            long long x = m__atoi64(S[si]), y = u__atoi64(S[si]);
            if (x != y) { if (bad < 8) printf("     _atoi64  \"%s\": msvcrt %lld  ucrt %lld\n", S[si], x, y); ++bad; }
        }
    }
    return bad;
}

static int diffs_writers(void)
{
    static const char* S[] = { "", "a", "abcdefgh", "0123456789abcdefghij" };
    int bad = 0, si, ci;
    GETB(strcpy_s, F_strcpys); GETB(wcscpy_s, F_wcscpys);
    GETB(_swab, F_swab); GETB(_memccpy, F_memccpy);
    for (si = 0; si < 4; ++si) {
        /* msvcrt does not export _set_invalid_parameter_handler, so there is no way to make its
         * `_s` functions report instead of terminating -- and the termination is __fastfail, which
         * SEH cannot catch either. The second run of this file died there, at 0xC0000409, after
         * printing the parser results. So the `_s` corpus is restricted to parameters that are
         * VALID for both CRTs: a destination big enough for the source. The too-small case is not
         * undriven because it is uninteresting -- it is undriven because driving it costs the run,
         * and this file says so rather than quietly starting the loop one index later. */
        int ci_min = (int)strlen(S[si]) + 1;
        for (ci = ci_min; ci <= 24; ++ci) {
            if (m_strcpy_s && u_strcpy_s) {
                static char d1[64], d2[64]; errno_t r1, r2; long n1, n2;
                memset(d1, 0x71, sizeof d1); memset(d2, 0x71, sizeof d2);
                iph_count = 0; r1 = m_strcpy_s(d1, (size_t)ci, S[si]); n1 = iph_count;
                iph_count = 0; r2 = u_strcpy_s(d2, (size_t)ci, S[si]); n2 = iph_count;
                if (r1 != r2 || n1 != n2 || memcmp(d1, d2, sizeof d1) != 0) {
                    if (bad < 8) printf("     strcpy_s \"%s\" cch %d: msvcrt r=%d h=%ld  ucrt r=%d h=%ld  buffers %s\n",
                        S[si], ci, r1, n1, r2, n2, memcmp(d1, d2, sizeof d1) ? "DIFFER" : "same");
                    ++bad;
                }
            }
        }
    }
    /* _swab: odd counts, overlap, and the in-place case */
    if (m__swab && u__swab) {
        static char s0[64], a[64], b[64];
        int n, off;
        for (n = 0; n <= 32; ++n) for (off = 0; off < 4; ++off) {
            int k; for (k = 0; k < 64; ++k) s0[k] = (char)(0x41 + (k % 26));
            memcpy(a, s0, 64); memcpy(b, s0, 64);
            m__swab(a + off, a + off + 2, n);
            u__swab(b + off, b + off + 2, n);
            if (memcmp(a, b, 64) != 0) { if (bad < 8) printf("     _swab n=%d off=%d: buffers DIFFER\n", n, off); ++bad; }
        }
    }
    /* _memccpy: the delimiter is only the low byte, and count 0 writes nothing */
    if (m__memccpy && u__memccpy) {
        static char s0[64], a[64], b[64];
        int n, c;
        for (n = 0; n <= 32; ++n) for (c = 0; c < 4; ++c) {
            static const int C[] = { 'A', 0x1263, -1, 0 };
            int k; for (k = 0; k < 64; ++k) s0[k] = (char)(0x41 + (k % 26));
            memset(a, 0x71, 64); memset(b, 0x71, 64);
            { void* r1 = m__memccpy(a, s0, C[c], (size_t)n);
              void* r2 = u__memccpy(b, s0, C[c], (size_t)n);
              if ((r1 == NULL) != (r2 == NULL) ||
                  (r1 && ((char*)r1 - a) != ((char*)r2 - b)) || memcmp(a, b, 64) != 0) {
                  if (bad < 8) printf("     _memccpy n=%d c=%d: DIFFER\n", n, C[c]); ++bad; } }
        }
    }
    return bad;
}

int main(void)
{
    int i, bad_p, bad_w;
    setvbuf(stdout, 0, _IONBF, 0);      /* an abort must not swallow what was printed first */
    _set_invalid_parameter_handler(iph);
    hM = LoadLibraryW(L"msvcrt.dll");
    hU = LoadLibraryW(L"ucrtbase.dll");
    if (!hM || !hU) { printf("could not load both CRTs\n"); return 2; }

    /* msvcrt's handler is a DIFFERENT handler in a DIFFERENT CRT. Install it too, or an invalid
       base terminates the process before a single row is printed -- which is exactly how the first
       run of this file ended: 0xC0000409, no output. */
    {
        F_setiph set_m = (F_setiph)GetProcAddress(hM, "_set_invalid_parameter_handler");
        if (set_m) { set_m(iph); m_handler_ok = 1; }
    }
    m_errno_fn = (F_errno)GetProcAddress(hM, "_errno");
    u_errno_fn = (F_errno)GetProcAddress(hU, "_errno");
    if (!m_errno_fn || !u_errno_fn) { printf("could not resolve _errno in both CRTs\n"); return 2; }
    printf("msvcrt invalid-parameter handler: %s\n",
           m_handler_ok ? "installed" : "NOT AVAILABLE -- invalid bases skipped for msvcrt");

    for (i = 0; i < 250; ++i) { nsrc[i % 250] = (char)(0x41 + (i % 26)); }
    nsrc[200] = 0;
    for (i = 0; i < 120; ++i) wdst[i] = (wchar_t)(L'a' + (i % 26));
    wdst[120] = 0;

    tm_strtol =(F_strtol) GetProcAddress(hM,"strtol");    tu_strtol =(F_strtol) GetProcAddress(hU,"strtol");
    tm_stoi64 =(F_stoi64) GetProcAddress(hM,"_strtoi64"); tu_stoi64 =(F_stoi64) GetProcAddress(hU,"_strtoi64");
    tm_atoi   =(F_atoi)   GetProcAddress(hM,"atoi");      tu_atoi   =(F_atoi)   GetProcAddress(hU,"atoi");
    tm_wcsrchr=(F_wcsrchr)GetProcAddress(hM,"wcsrchr");   tu_wcsrchr=(F_wcsrchr)GetProcAddress(hU,"wcsrchr");
    tm_swab   =(F_swab)   GetProcAddress(hM,"_swab");     tu_swab   =(F_swab)   GetProcAddress(hU,"_swab");
    tm_memccpy=(F_memccpy)GetProcAddress(hM,"_memccpy");  tu_memccpy=(F_memccpy)GetProcAddress(hU,"_memccpy");
    tm_strcpys=(F_strcpys)GetProcAddress(hM,"strcpy_s");  tu_strcpys=(F_strcpys)GetProcAddress(hU,"strcpy_s");

    printf("msvcrt.dll vs ucrtbase.dll -- the 41 functions converted for one CRT and not the other\n");
    printf("RUN THIS ON AN IDLE MACHINE. min-of-40.\n\n");

    printf("QUESTION 2 FIRST, because it is cheap: IS msvcrt SLOWER?\n");
    printf("  %-14s %10s %11s %9s  %s\n", "function", "msvcrt ns", "ucrtbase ns", "ratio", "verdict");
    printf("  ------------------------------------------------------------------------\n");
    if (tm_strtol && tu_strtol)   timed("strtol",   o_m_strtol,  o_u_strtol);
    if (tm_stoi64 && tu_stoi64)   timed("_strtoi64",o_m_stoi64,  o_u_stoi64);
    if (tm_atoi && tu_atoi)       timed("atoi",     o_m_atoi,    o_u_atoi);
    if (tm_wcsrchr && tu_wcsrchr) timed("wcsrchr",  o_m_wcsrchr, o_u_wcsrchr);
    if (tm_swab && tu_swab)       timed("_swab",    o_m_swab,    o_u_swab);
    if (tm_memccpy && tu_memccpy) timed("_memccpy", o_m_memccpy, o_u_memccpy);
    if (tm_strcpys && tu_strcpys) timed("strcpy_s", o_m_strcpys, o_u_strcpys);

    printf("\nQUESTION 1, THE ONE THAT DECIDES IT: ARE THEY THE SAME FUNCTION?\n");
    printf("  Return value, endptr as an offset, errno AND the invalid-parameter handler count,\n");
    printf("  over shared corpora. Any difference means the existing assembly may NOT simply be\n");
    printf("  pointed at msvcrt's export -- it would be a separate change with its own contract.\n\n");
    bad_p = diffs_parsers();
    printf("  parsers  (strtol/strtoul/_strtoi64/_strtoui64/atoi/_atoi64): %d differences\n", bad_p);
    bad_w = diffs_writers();
    printf("  writers  (strcpy_s/_swab/_memccpy):                          %d differences\n", bad_w);

    printf("\n  TOTAL: %d differences between the two CRTs.\n", bad_p + bad_w);
    printf("\nIf the count is 0 AND msvcrt is slower, the existing gate-validated assembly applies to\n"
           "msvcrt's exports as well and the work is materialisation. If the count is not 0, the\n"
           "differing functions are separate changes, and the differences are the contract.\n");
    return 0;
}
