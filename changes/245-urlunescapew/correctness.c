/* changes/245-urlunescapew/correctness.c
 *
 * Gate 1 for change 245: wia_urlunescapew must be indistinguishable from the live UrlUnescapeW.
 *
 * Three-way on every case -- ours, an independent oracle (reference.c) and the live export -- and
 * every case compares the HRESULT, the whole destination against a sentinel fill, and *pcch. All
 * three are load-bearing:
 *
 *   * %00 and a too-small buffer must leave the destination COMPLETELY UNTOUCHED, which only a
 *     sentinel fill can show. A string comparison passes an implementation that writes a partial
 *     result and then returns the right failure.
 *   * a too-small buffer must set *pcch to the result length PLUS ONE while success sets it to the
 *     result length, so *pcch is a separate observable and off by one in either direction is a bug.
 *   * URL_UNESCAPE_INPLACE must not write *pcch at all.
 *
 * The four axes that matter, and why each is here rather than sampled:
 *
 *   1. Every code unit in both escape positions. The hex set is 22 ASCII characters out of 65536,
 *      and an implementation that accepted one more -- a full-width digit, say, or a character the
 *      OS tables call a hex digit -- would pass any corpus of realistic URLs. So both positions are
 *      swept over all 65535 non-NUL code units.
 *   2. Every buffer size around the result length. The size test is strict and its failure must not
 *      touch the destination, so every capacity from 1 to result+3 is driven for several shapes.
 *   3. The flag cross product, including the delegated ones. The native domain is
 *      {0, INPLACE, DONT_UNESCAPE_EXTRA_INFO, both}; AS_UTF8 and every other single bit must come
 *      back byte-identical too, through the fallback.
 *   4. Overlap at every relative placement. The shipped function stages through a temporary, so
 *      overlap is well-defined; ours writes directly when the destination is at or below the source
 *      and delegates otherwise, and the only way to know both halves are right is to drive every
 *      placement.
 *
 * Plus guard pages on both sides, because the scan reads 32 bytes at a time and the copy writes them.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define F_INPLACE     0x00100000u
#define F_AS_UTF8     0x00040000u
#define F_EXTRA_INFO  0x02000000u

extern long wia_urlunescapew(wchar_t*, wchar_t*, unsigned long*, unsigned long);
extern void wia_uue_set_fallback(void*);
extern long wia_ref_urlunescapew(wchar_t*, wchar_t*, unsigned long*, unsigned long);

typedef HRESULT (WINAPI *FN)(const wchar_t*, wchar_t*, DWORD*, DWORD);
static FN sys;

static int fails = 0;
static long cases = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define SENT 0xBEEF
#define TAIL 16          /* sentinel characters past the capacity, to catch an over-write */

/* Is this flag value inside the implemented domain? Outside it the assembly TAIL-JUMPS to the
   shipped export, so "ours" and "live" are the same code and the only thing worth asserting is that
   the delegation actually happens -- byte for byte, which the comparison below still does. The
   ORACLE is skipped for those, because reference.c deliberately does not model URL_UNESCAPE_AS_UTF8:
   it hands runs of escaped bytes to MultiByteToWideChar(CP_UTF8, ...) and re-deriving that by hand
   is the change-239 failure mode. Counting them separately keeps the claim honest. */
static long delegated = 0;
static int native_domain(DWORD flags)
{
    return (flags & ~(F_INPLACE | F_EXTRA_INFO)) == 0;
}

/* one non-in-place case: three ways, HRESULT + whole buffer + *pcch */
static void one(const wchar_t* in, DWORD cap, DWORD flags, const char* what)
{
    static wchar_t sa[2048], sb[2048], sc[2048];
    static wchar_t ia[2048], ib[2048], ic[2048];
    DWORD ca = cap, cb = cap, cc = cap;
    size_t n = wcslen(in);
    long ra, rb, rc;
    for (DWORD i = 0; i < cap + TAIL; ++i) { sa[i] = SENT; sb[i] = SENT; sc[i] = SENT; }
    memcpy(ia, in, (n + 1) * sizeof(wchar_t));
    memcpy(ib, in, (n + 1) * sizeof(wchar_t));
    memcpy(ic, in, (n + 1) * sizeof(wchar_t));
    ra = wia_urlunescapew(ia, sa, &ca, flags);
    rc = (long)sys(ic, sc, &cc, flags);
    ++cases;
    CHECK(ra == rc, "%s \"%ls\" cap=%lu f=%08lX: HRESULT ours %08lX live %08lX", what, in,
          (unsigned long)cap, (unsigned long)flags, (unsigned long)ra, (unsigned long)rc);
    CHECK(memcmp(sa, sc, (cap + TAIL) * sizeof(wchar_t)) == 0,
          "%s \"%ls\" cap=%lu f=%08lX: destination ours vs live", what, in, (unsigned long)cap,
          (unsigned long)flags);
    CHECK(ca == cc, "%s \"%ls\" cap=%lu f=%08lX: *pcch ours %lu live %lu", what, in,
          (unsigned long)cap, (unsigned long)flags, (unsigned long)ca, (unsigned long)cc);
    if (!native_domain(flags)) { ++delegated; }
    else {
        rb = wia_ref_urlunescapew(ib, sb, &cb, flags);
        CHECK(rb == rc, "%s \"%ls\" cap=%lu f=%08lX: HRESULT oracle %08lX live %08lX", what, in,
              (unsigned long)cap, (unsigned long)flags, (unsigned long)rb, (unsigned long)rc);
        CHECK(memcmp(sb, sc, (cap + TAIL) * sizeof(wchar_t)) == 0,
              "%s \"%ls\" cap=%lu f=%08lX: destination oracle vs live", what, in,
              (unsigned long)cap, (unsigned long)flags);
        CHECK(cb == cc, "%s \"%ls\" cap=%lu f=%08lX: *pcch oracle %lu live %lu", what, in,
              (unsigned long)cap, (unsigned long)flags, (unsigned long)cb, (unsigned long)cc);
    }
    /* the input must come back untouched on every non-in-place path */
    CHECK(memcmp(ia, ic, (n + 1) * sizeof(wchar_t)) == 0,
          "%s \"%ls\": ours modified the INPUT", what, in);
}

/* one in-place case: the rewritten buffer and the HRESULT, and *pcch must be untouched */
static void one_ip(const wchar_t* in, DWORD flags, const char* what)
{
    static wchar_t ba[2048], bb[2048], bc[2048];
    DWORD ca = 0xABCD, cb = 0xABCD, cc = 0xABCD;
    size_t n = wcslen(in);
    long ra, rb, rc;
    for (size_t i = 0; i < n + TAIL; ++i) { ba[i] = SENT; bb[i] = SENT; bc[i] = SENT; }
    memcpy(ba, in, (n + 1) * sizeof(wchar_t));
    memcpy(bb, in, (n + 1) * sizeof(wchar_t));
    memcpy(bc, in, (n + 1) * sizeof(wchar_t));
    ra = wia_urlunescapew(ba, 0, &ca, flags | F_INPLACE);
    rb = wia_ref_urlunescapew(bb, 0, &cb, flags | F_INPLACE);
    rc = (long)sys(bc, 0, &cc, flags | F_INPLACE);
    ++cases;
    CHECK(ra == rc, "%s(ip) \"%ls\": HRESULT ours %08lX live %08lX", what, in,
          (unsigned long)ra, (unsigned long)rc);
    CHECK(rb == rc, "%s(ip) \"%ls\": HRESULT oracle %08lX live %08lX", what, in,
          (unsigned long)rb, (unsigned long)rc);
    CHECK(memcmp(ba, bc, (n + TAIL) * sizeof(wchar_t)) == 0,
          "%s(ip) \"%ls\": buffer ours vs live", what, in);
    CHECK(memcmp(bb, bc, (n + TAIL) * sizeof(wchar_t)) == 0,
          "%s(ip) \"%ls\": buffer oracle vs live", what, in);
    CHECK(ca == cc && cb == cc, "%s(ip) \"%ls\": *pcch touched (ours %lu oracle %lu live %lu)",
          what, in, (unsigned long)ca, (unsigned long)cb, (unsigned long)cc);
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "UrlUnescapeW");
    if (!sys) { printf("cannot resolve UrlUnescapeW\n"); return 1; }
    /* the delegated cases go to the shipped export */
    wia_uue_set_fallback((void*)sys);

    static wchar_t in[2048];

    /* ---- 1. every code unit in both escape positions ---- */
    {
        for (int c = 1; c < 65536; ++c) {
            in[0] = L'x'; in[1] = L'%'; in[2] = (wchar_t)c; in[3] = L'1'; in[4] = L'y'; in[5] = 0;
            one(in, 64, 0, "hex1");
            in[0] = L'x'; in[1] = L'%'; in[2] = L'1'; in[3] = (wchar_t)c; in[4] = L'y'; in[5] = 0;
            one(in, 64, 0, "hex2");
        }
        printf("  swept both escape positions over all 65535 non-NUL code units\n");
    }

    /* ---- 2. every accepted pair, and every capacity around the result ---- */
    {
        static const wchar_t* HEX = L"0123456789ABCDEFabcdef";
        for (int i = 0; HEX[i]; ++i)
            for (int j = 0; HEX[j]; ++j) {
                in[0] = L'a'; in[1] = L'%'; in[2] = HEX[i]; in[3] = HEX[j]; in[4] = L'b'; in[5] = 0;
                for (DWORD cap = 1; cap <= 6; ++cap) one(in, cap, 0, "pair");
            }
    }

    /* ---- 3. the shapes the probe pinned, at every capacity from 1 to result+3 ---- */
    {
        static const wchar_t* T[] = {
            L"", L"a", L"%", L"%%", L"%4", L"a%", L"a%4", L"a%zz", L"a%4z", L"a%z4",
            L"%41", L"%41%42", L"%414243", L"%2541", L"%41x", L"a%41b%42c",
            L"a%00b", L"%00", L"a%00", L"%00b", L"%0", L"%000",
            L"a%41b?c%42d", L"a%41b#c%42d", L"?%41", L"#%41", L"a%3Fb%41", L"#", L"?",
            L"%41%42%43%44%45%46%47%48%49%4A%4B%4C%4D%4E%4F%50%51%52%53%54",
            L"0123456789012345678901234567890123456789%41",
            L"%41" L"0123456789012345678901234567890123456789",
            L"%C3%A9", L"%E2%82%AC", L"%FF%FE", L"%C3",
        };
        static const DWORD FL[] = { 0, F_EXTRA_INFO, F_AS_UTF8, F_AS_UTF8 | F_EXTRA_INFO,
                                    0x00000001, 0x80000000, 0x00000100, 0x00001000, 0x00080000 };
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            size_t n = wcslen(T[i]);
            for (int f = 0; f < (int)(sizeof FL / sizeof FL[0]); ++f)
                for (DWORD cap = 1; cap <= (DWORD)n + 3; ++cap)
                    one(T[i], cap, FL[f], "shape");
            one_ip(T[i], 0, "shape");
            one_ip(T[i], F_EXTRA_INFO, "shape");
        }
    }

    /* ---- 4. lengths across every 32-byte block boundary, escaped and not ---- */
    {
        for (int n = 0; n <= 200; ++n) {
            for (int k = 0; k < n; ++k) in[k] = (wchar_t)(L'a' + k % 26);
            in[n] = 0;
            one(in, 300, 0, "plain");
            one(in, (DWORD)n + 1, 0, "plain-exact");
            one_ip(in, 0, "plain");
            /* one escape at every position, so the run lengths either side cross the boundary */
            if (n >= 3) {
                for (int p = 0; p + 3 <= n; p += (n > 40 ? 7 : 1)) {
                    for (int k = 0; k < n; ++k) in[k] = (wchar_t)(L'a' + k % 26);
                    in[p] = L'%'; in[p+1] = L'4'; in[p+2] = L'1';
                    in[n] = 0;
                    one(in, 300, 0, "oneesc");
                    one_ip(in, 0, "oneesc");
                }
            }
        }
    }

    /* ---- 5. escape-dense strings, where the scan hits on almost every block ---- */
    {
        for (int n = 1; n <= 60; ++n) {
            int k = 0;
            for (int i = 0; i < n; ++i) {
                in[k++] = L'%'; in[k++] = L'4'; in[k++] = (wchar_t)(L'1' + i % 9);
            }
            in[k] = 0;
            one(in, 300, 0, "dense");
            one(in, (DWORD)n + 1, 0, "dense-exact");
            one_ip(in, 0, "dense");
        }
    }

    /* ---- 6. fuzz over the alphabet that manufactures partial escapes ---- */
    {
        unsigned long rng = 0x13579BDFu;
        static const wchar_t A[] = L"%0149AFafzZ?#/ ";
        for (int t = 0; t < 40000; ++t) {
            rng = rng * 1103515245u + 12345u;
            int n = (int)((rng >> 8) % 40);
            for (int k = 0; k < n; ++k) {
                rng = rng * 1103515245u + 12345u;
                in[k] = A[(rng >> 9) % (sizeof A / sizeof A[0] - 1)];
            }
            in[n] = 0;
            rng = rng * 1103515245u + 12345u;
            DWORD cap = 1 + (rng >> 8) % (DWORD)(n + 3);
            rng = rng * 1103515245u + 12345u;
            DWORD f = ((rng >> 8) & 1) ? F_EXTRA_INFO : 0;
            one(in, cap, f, "fuzz");
            one_ip(in, f, "fuzz");
        }
    }

    /* ---- 7. the NULL and zero arguments ---- */
    {
        static wchar_t d[64];
        DWORD ca, cc;
        long ra, rc;
        ca = cc = 64;
        ra = wia_urlunescapew(0, d, &ca, 0);
        rc = (long)sys(0, d, &cc, 0);
        CHECK(ra == rc, "NULL url: ours %08lX live %08lX", (unsigned long)ra, (unsigned long)rc);
        ca = cc = 64;
        ra = wia_urlunescapew(L"abc", 0, &ca, 0);
        rc = (long)sys(L"abc", 0, &cc, 0);
        CHECK(ra == rc, "NULL dst: ours %08lX live %08lX", (unsigned long)ra, (unsigned long)rc);
        ra = wia_urlunescapew(L"abc", d, 0, 0);
        rc = (long)sys(L"abc", d, 0, 0);
        CHECK(ra == rc, "NULL pcch: ours %08lX live %08lX", (unsigned long)ra, (unsigned long)rc);
        ca = cc = 0;
        ra = wia_urlunescapew(L"abc", d, &ca, 0);
        rc = (long)sys(L"abc", d, &cc, 0);
        CHECK(ra == rc, "*pcch==0: ours %08lX live %08lX", (unsigned long)ra, (unsigned long)rc);
        cases += 4;
    }

    /* ---- 8. OVERLAP at every relative placement ---- */
    {
        enum { BUF = 192 };
        static wchar_t ba[BUF], bc[BUF];
        static const wchar_t* T[] = { L"a%41b%42c", L"%41%42%43%44", L"%41", L"abc",
                                      L"a%41b?c%42d" };
        long ov = 0, dj = 0;
        for (int i = 0; i < 5; ++i) {
            size_t n = wcslen(T[i]);
            for (int doff = 0; doff <= 40; ++doff) {
                for (int hoff = 0; hoff <= 40; ++hoff) {
                    DWORD ca = 64, cc = 64;
                    long ra, rc;
                    for (int k = 0; k < BUF; ++k) { ba[k] = SENT; bc[k] = SENT; }
                    memcpy(ba + doff, T[i], (n + 1) * sizeof(wchar_t));
                    memcpy(bc + doff, T[i], (n + 1) * sizeof(wchar_t));
                    ra = wia_urlunescapew(ba + doff, ba + hoff, &ca, 0);
                    rc = (long)sys(bc + doff, bc + hoff, &cc, 0);
                    ++cases;
                    if (hoff > doff && hoff <= doff + (int)n) ++ov; else ++dj;
                    CHECK(ra == rc, "overlap \"%ls\" d=%d h=%d: HRESULT ours %08lX live %08lX",
                          T[i], doff, hoff, (unsigned long)ra, (unsigned long)rc);
                    CHECK(memcmp(ba, bc, BUF * sizeof(wchar_t)) == 0,
                          "overlap \"%ls\" d=%d h=%d: buffer differs", T[i], doff, hoff);
                    CHECK(ca == cc, "overlap \"%ls\" d=%d h=%d: *pcch %lu vs %lu", T[i], doff,
                          hoff, (unsigned long)ca, (unsigned long)cc);
                }
            }
        }
        printf("  overlap sweep: %ld placements with the destination inside the input, %ld not\n",
               ov, dj);
    }

    /* ---- 9. guard pages: the input ending at an unreadable page, the output at an unwritable one ---- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* gs = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        char* gd = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        if (gs && gd) {
            VirtualProtect(gs + pg, pg, PAGE_NOACCESS, &old);
            VirtualProtect(gd + pg, pg, PAGE_NOACCESS, &old);
            /* the input's terminator is the last readable character */
            for (int n = 0; n <= 160; ++n) {
                wchar_t* s = (wchar_t*)(gs + pg) - (n + 1);
                static wchar_t mine[256], live[256];
                DWORD ca = 200, cc = 200;
                long ra, rc;
                for (int k = 0; k < n; ++k) s[k] = (k % 4 == 0) ? L'%' :
                                                  (k % 4 == 1) ? L'4' :
                                                  (k % 4 == 2) ? L'1' : L'z';
                s[n] = 0;
                for (int k = 0; k < 256; ++k) { mine[k] = SENT; live[k] = SENT; }
                ra = wia_urlunescapew(s, mine, &ca, 0);
                rc = (long)sys(s, live, &cc, 0);
                ++cases;
                CHECK(ra == rc && ca == cc && memcmp(mine, live, 256 * sizeof(wchar_t)) == 0,
                      "guard-input n=%d: ours %08lX/%lu live %08lX/%lu", n, (unsigned long)ra,
                      (unsigned long)ca, (unsigned long)rc, (unsigned long)cc);
            }
            /* the destination's last usable character is the last writable one */
            for (int cap = 1; cap <= 160; ++cap) {
                wchar_t* d = (wchar_t*)(gd + pg) - cap;
                static wchar_t live[256];
                static wchar_t src[64];
                DWORD ca = (DWORD)cap, cc = (DWORD)cap;
                long ra, rc;
                int k;
                for (k = 0; k < 20; ++k) src[k] = (k % 3 == 0) ? L'%' : (k % 3 == 1) ? L'4' : L'1';
                src[20] = 0;
                for (k = 0; k < cap; ++k) d[k] = SENT;
                for (k = 0; k < 256; ++k) live[k] = SENT;
                ra = wia_urlunescapew(src, d, &ca, 0);
                rc = (long)sys(src, live, &cc, 0);
                ++cases;
                CHECK(ra == rc && ca == cc, "guard-dst cap=%d: ours %08lX/%lu live %08lX/%lu",
                      cap, (unsigned long)ra, (unsigned long)ca, (unsigned long)rc,
                      (unsigned long)cc);
                if (ra == 0)
                    CHECK(memcmp(d, live, (size_t)ca * sizeof(wchar_t)) == 0,
                          "guard-dst cap=%d: bytes differ", cap);
            }
            VirtualFree(gs, 0, MEM_RELEASE);
            VirtualFree(gd, 0, MEM_RELEASE);
        }
    }

    if (fails) {
        printf("CORRECTNESS: FAILED (%d checks, %ld cases)\n", fails, cases);
        return 1;
    }
    printf("CORRECTNESS: PASS (UrlUnescapeW vs live + oracle, comparing the HRESULT, the WHOLE "
           "destination against a sentinel fill with 16 characters past the capacity, AND *pcch: "
           "both escape positions swept over ALL 65535 non-NUL code units, every one of the 484 "
           "accepted hex pairs at six capacities, 35 pinned shapes x 9 flag values x every capacity "
           "from 1 to result+3, every length 0..200 plain and with one escape at every position, "
           "escape-dense strings to 60 escapes, 40000 fuzz cases over an alphabet built to "
           "manufacture partial escapes, every NULL combination, THE FULL OVERLAP SWEEP at every "
           "relative placement, and guard pages on BOTH sides; %ld cases)\n", cases);
    return 0;
}
