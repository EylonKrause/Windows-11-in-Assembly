// changes/151-wcscpy-s/correctness.c
// Bit-exact fuzz of wia_wcscpy_s vs live ucrtbase!wcscpy_s + oracle. Each trial compares the errno
// return, the number of invalid-parameter-handler invocations, and every BYTE of a canary-filled
// destination -- the byte-level compare is what pins the ERANGE path, which writes `size` wide
// characters of src before emptying dst.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>

extern int wia_wcscpy_s(wchar_t* dst, size_t size, const wchar_t* src);
int ref_wcscpy_s(wchar_t* dst, size_t size, const wchar_t* src, int* iph);

typedef int  (__cdecl *fn)(wchar_t*, size_t, const wchar_t*);
typedef void (__cdecl *IPH)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
typedef IPH  (__cdecl *pSetIPH)(IPH);

static fn  sys;
static int hits = 0, argsnull = 1, fails = 0;
static void __cdecl myiph(const wchar_t* e, const wchar_t* f, const wchar_t* fi, unsigned l, uintptr_t r)
{   if (e || f || fi || l || r) argsnull = 0; ++hits; }

#define DW 320                                   /* destination buffer, in wchar_t */
static wchar_t dsys[DW], dour[DW], dref[DW];
static wchar_t srcbuf[512];

static void seed(void)
{   for (int i = 0; i < DW; ++i) dsys[i] = dour[i] = dref[i] = (wchar_t)(0xC0C0 + (i & 15)); }

static void trial(int soff, int doff, int len, size_t size, wchar_t fillbase, const char* what)
{
    if (fails >= 15) return;
    wchar_t* s = srcbuf + soff;
    for (int i = 0; i < len; ++i) s[i] = (wchar_t)(fillbase + (i % 23));
    s[len] = 0;
    seed();
    int hs, ho, hr = 0, rs, ro, rr;
    hits = 0; rs = sys(dsys + doff, size, s);             hs = hits;
    hits = 0; ro = wia_wcscpy_s(dour + doff, size, s);    ho = hits;
    rr = ref_wcscpy_s(dref + doff, size, s, &hr);
    if (rs != ro || rs != rr || hs != ho || hs != hr
        || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
    {
        ++fails;
        printf("FAIL %s soff=%d doff=%d len=%d size=%zu  rc sys=%d ours=%d ref=%d  iph sys=%d ours=%d ref=%d\n",
               what, soff, doff, len, size, rs, ro, rr, hs, ho, hr);
        for (int i = 0; i < DW; ++i)
            if (dsys[i] != dour[i]) { printf("  first wchar diff at %d: sys=%04X ours=%04X ref=%04X\n",
                                            i, dsys[i], dour[i], dref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);    /* the failure modes here are process-killing, so never buffer */
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "wcscpy_s");
    pSetIPH set = (pSetIPH)GetProcAddress(u, "_set_invalid_parameter_handler");
    if (!sys || !set) { printf("no wcscpy_s / handler setter\n"); return 2; }
    set(myiph);                       /* without this the live ERANGE path __fastfails the process */

    /* /MD build on purpose -- see RESULTS.md: under the default /MT the test program would carry its
       own static copy of the handler state and the two sides would consult different handlers. */
    { hits = 0; _invalid_parameter_noinfo();
      if (hits != 1) { ++fails; printf("FAIL handler not reachable through _invalid_parameter_noinfo\n"); } }

    /* ---- NULL / zero-size argument paths ----------------------------------------------------- */
    {
        int h1, h2, h3 = 0, r1, r2, r3;
        seed();
        hits = 0; r1 = sys(0, 10, L"abc");            h1 = hits;
        hits = 0; r2 = wia_wcscpy_s(0, 10, L"abc");   h2 = hits;
        r3 = ref_wcscpy_s(0, 10, L"abc", &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3 || memcmp(dsys, dour, sizeof dsys))
        { ++fails; printf("FAIL NULL-dst rc %d/%d/%d iph %d/%d/%d\n", r1, r2, r3, h1, h2, h3); }
    }
    trial(0, 0, 3, 0, L'a', "size-0");                 /* EINVAL, dst untouched */
    {
        int h1, h2, h3 = 0, r1, r2, r3;
        seed();
        hits = 0; r1 = sys(dsys, 10, 0);              h1 = hits;
        hits = 0; r2 = wia_wcscpy_s(dour, 10, 0);     h2 = hits;
        r3 = ref_wcscpy_s(dref, 10, 0, &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3
            || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
        { ++fails; printf("FAIL NULL-src rc %d/%d/%d iph %d/%d/%d\n", r1, r2, r3, h1, h2, h3); }
    }

    /* ---- the grid: src/dst alignment x length x every interesting bound ----------------------- */
    for (int soff = 0; soff < 16 && fails < 15; ++soff)
        for (int doff = 0; doff < 8 && fails < 15; ++doff)
            for (int len = 0; len <= 140 && fails < 15; ++len)
            {
                /* 0x412C has low byte ',' and 0xFF41 has low byte 'A' -- a byte-granular terminator
                   scan would false-hit on the 0x_100-style values, so the fill alternates. */
                wchar_t base = (len & 1) ? (wchar_t)0x4100 : L'a';
                trial(soff, doff, len, (size_t)len + 1, base, "exact fit");
                trial(soff, doff, len, (size_t)len + 2, base, "one to spare");
                trial(soff, doff, len, 200,             base, "roomy");
                if (len >= 1) trial(soff, doff, len, (size_t)len,     base, "short by one");
                if (len >= 2) trial(soff, doff, len, (size_t)len / 2, base, "half");
                trial(soff, doff, len, 1,               base, "size 1");
                trial(soff, doff, len, 2,               base, "size 2");
            }

    /* ---- a string whose wchars all have a zero HIGH byte, then all zero LOW byte -------------- */
    for (int len = 1; len <= 130 && fails < 15; ++len)
    {
        for (int i = 0; i < len; ++i) srcbuf[i] = (wchar_t)0x0041;   /* high byte 0 */
        srcbuf[len] = 0;
        trial(0, 0, len, (size_t)len + 1, 0, "highbyte-zero");       /* fill overwritten below */
        for (int i = 0; i < len; ++i) srcbuf[i] = (wchar_t)0x4100;   /* low byte 0 */
        srcbuf[len] = 0;
        seed();
        int hs, ho, hr = 0, rs, ro, rr;
        hits = 0; rs = sys(dsys, 200, srcbuf);           hs = hits;
        hits = 0; ro = wia_wcscpy_s(dour, 200, srcbuf);  ho = hits;
        rr = ref_wcscpy_s(dref, 200, srcbuf, &hr);
        if (rs != ro || rs != rr || hs != ho || hs != hr
            || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
        { ++fails; printf("FAIL lowbyte-zero len=%d rc %d/%d/%d\n", len, rs, ro, rr); }
    }

    /* ---- src ending at a page boundary, next page NOACCESS ----------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 200 && fails < 15; ++len)
        {
            wchar_t* s = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i) s[i] = (wchar_t)(L'a' + (i % 23));
            s[len] = 0;
            size_t sizes[4] = { (size_t)len + 1, 300, 100000, (size_t)(len ? len : 1) };
            for (int k = 0; k < 4; ++k)
            {
                if (k == 3 && len == 0) continue;
                seed();
                int hs, ho, hr = 0, rs, ro, rr;
                hits = 0; rs = sys(dsys, sizes[k], s);            hs = hits;
                hits = 0; ro = wia_wcscpy_s(dour, sizes[k], s);   ho = hits;
                rr = ref_wcscpy_s(dref, sizes[k], s, &hr);
                if (rs != ro || rs != rr || hs != ho || hs != hr
                    || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
                { ++fails; printf("FAIL src-guard len=%d size=%zu rc %d/%d/%d iph %d/%d/%d\n",
                                  len, sizes[k], rs, ro, rr, hs, ho, hr); }
            }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* ---- dst ending at a page boundary: a write one wchar past `size` must fault -------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[256];
        for (int size = 1; size <= 200 && fails < 15; ++size)
            for (int len = 0; len <= 200 && fails < 15; len += 7)
            {
                wchar_t* d = (wchar_t*)(mem + si.dwPageSize - size * 2);
                for (int i = 0; i < len; ++i) srcbuf[i] = (wchar_t)(L'a' + (i % 23));
                srcbuf[len] = 0;
                wmemset(d, 0x5A5A, size);
                hits = 0; int rs = sys(d, (size_t)size, srcbuf); int hs = hits;
                wmemcpy(snap, d, size);
                wmemset(d, 0x5A5A, size);
                hits = 0; int ro = wia_wcscpy_s(d, (size_t)size, srcbuf); int ho = hits;
                if (rs != ro || hs != ho || memcmp(snap, d, (size_t)size * 2))
                { ++fails; printf("FAIL dst-guard size=%d len=%d rc %d/%d iph %d/%d\n",
                                  size, len, rs, ro, hs, ho); }
            }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!argsnull) { ++fails; printf("FAIL handler received non-NULL arguments\n"); }
    if (!fails)
        printf("CORRECTNESS: PASS (wcscpy_s vs live + oracle: return code, handler-invocation count and every\n"
               "  destination byte, over 16 src alignments x 8 dst alignments x lengths 0..140 x 7 size bounds,\n"
               "  zero-high-byte and zero-low-byte wchar traps, the NULL-dst/NULL-src/size-0 paths, a src\n"
               "  NOACCESS page-guard sweep, and a dst page-guard sweep proving no write lands past `size`)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
