// changes/155-wcsncpy-s/correctness.c
// Bit-exact fuzz of wia_wcsncpy_s vs live ucrtbase!wcsncpy_s + oracle. Every trial compares the
// errno return, the number of invalid-parameter-handler invocations, and every byte of a
// canary-filled destination -- which is what pins the two DIFFERENT truncation behaviours (ERANGE
// empties the string; _TRUNCATE terminates the last byte and stays silent).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <wchar.h>

extern int wia_wcsncpy_s(wchar_t* dst, size_t size, const wchar_t* src, size_t count);
int ref_wcsncpy_s(wchar_t* dst, size_t size, const wchar_t* src, size_t count, int* iph);

typedef int  (__cdecl *fn)(wchar_t*, size_t, const wchar_t*, size_t);
typedef void (__cdecl *IPH)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
typedef IPH  (__cdecl *pSetIPH)(IPH);

static fn  sys;
static int hits = 0, argsnull = 1, fails = 0;
static void __cdecl myiph(const wchar_t* e, const wchar_t* f, const wchar_t* fi, unsigned l, uintptr_t r)
{   if (e || f || fi || l || r) argsnull = 0; ++hits; }

#define DSZ 512
static wchar_t dsys[DSZ], dour[DSZ], dref[DSZ];
static wchar_t srcbuf[512];

static void seed(void)
{   for (int i = 0; i < DSZ; ++i) dsys[i] = dour[i] = dref[i] = (wchar_t)(0xC0C0 + (i & 15)); }

static void trial(int soff, int doff, int len, size_t size, size_t count, const char* what)
{
    if (fails >= 15) return;
    wchar_t* s = srcbuf + soff;
    /* alternate a zero-high-byte and a zero-low-byte family: either would false-hit a
       byte-granular terminator scan */
    for (int i = 0; i < len; ++i) s[i] = (wchar_t)(((len & 1) ? 0x4100 : 0x0041) + (i % 23));
    s[len] = 0;
    seed();
    int hs, ho, hr = 0, rs, ro, rr;
    hits = 0; rs = sys(dsys + doff, size, s, count);            hs = hits;
    hits = 0; ro = wia_wcsncpy_s(dour + doff, size, s, count);  ho = hits;
    rr = ref_wcsncpy_s(dref + doff, size, s, count, &hr);
    if (rs != ro || rs != rr || hs != ho || hs != hr
        || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
    {
        ++fails;
        printf("FAIL %s soff=%d doff=%d len=%d size=%zu count=%zd  rc %d/%d/%d  iph %d/%d/%d\n",
               what, soff, doff, len, size, (ptrdiff_t)count, rs, ro, rr, hs, ho, hr);
        for (int i = 0; i < DSZ; ++i)
            if (dsys[i] != dour[i]) { printf("  first wchar diff at %d: sys=%04X ours=%04X ref=%04X\n",
                                            i, dsys[i], dour[i], dref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "wcsncpy_s");
    pSetIPH set = (pSetIPH)GetProcAddress(u, "_set_invalid_parameter_handler");
    if (!sys || !set) { printf("no wcsncpy_s / handler setter\n"); return 2; }
    set(myiph);
    { hits = 0; _invalid_parameter_noinfo();
      if (hits != 1) { ++fails; printf("FAIL handler not reachable through _invalid_parameter_noinfo\n"); } }

    /* ---- the argument-validation paths, each checked for return, handler count and writes ------ */
    {
        struct { wchar_t* d; size_t size; const wchar_t* s; size_t count; const char* tag; } A[] = {
            { 0,    10, L"abc", 3,  "NULL dst" },
            { 0,     0, L"abc", 0,  "NULL dst + size 0 + count 0 (the documented no-op)" },
            { 0,    10, L"abc", 0,  "NULL dst + count 0, size != 0" },
            { 0,     0, L"abc", 3,  "NULL dst + size 0, count != 0" },
        };
        for (int i = 0; i < 4; ++i)
        {
            seed();
            int h1, h2, h3 = 0, r1, r2, r3;
            hits = 0; r1 = sys(A[i].d, A[i].size, A[i].s, A[i].count);            h1 = hits;
            hits = 0; r2 = wia_wcsncpy_s(A[i].d, A[i].size, A[i].s, A[i].count);  h2 = hits;
            r3 = ref_wcsncpy_s(A[i].d, A[i].size, A[i].s, A[i].count, &h3);
            if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3 || memcmp(dsys, dour, sizeof dsys))
            { ++fails; printf("FAIL %s rc %d/%d/%d iph %d/%d/%d\n", A[i].tag, r1, r2, r3, h1, h2, h3); }
        }
    }
    trial(0, 0, 3, 0, 3, "size 0");
    trial(0, 0, 3, 0, 0, "size 0, count 0");     /* dst non-NULL, so still EINVAL */
    /* NULL src, with and without count 0 -- count 0 must NOT invoke the handler */
    for (int k = 0; k < 2; ++k)
    {
        seed();
        size_t cnt = k ? 0 : 3;
        int h1, h2, h3 = 0, r1, r2, r3;
        hits = 0; r1 = sys(dsys, 10, 0, cnt);            h1 = hits;
        hits = 0; r2 = wia_wcsncpy_s(dour, 10, 0, cnt);  h2 = hits;
        r3 = ref_wcsncpy_s(dref, 10, 0, cnt, &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3
            || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
        { ++fails; printf("FAIL NULL-src count=%zu rc %d/%d/%d iph %d/%d/%d\n", cnt, r1, r2, r3, h1, h2, h3); }
    }

    /* ---- the grid: alignment x length x every interesting (size, count) pair ------------------- */
    for (int soff = 0; soff < 16 && fails < 15; ++soff)
        for (int doff = 0; doff < 8 && fails < 15; ++doff)
            for (int len = 0; len <= 90 && fails < 15; ++len)
            {
                size_t L = (size_t)len;
                /* count around the string length, size around what that needs */
                for (int dc = -2; dc <= 2; ++dc)
                {
                    if ((ptrdiff_t)L + dc < 0) continue;
                    size_t c = L + dc;
                    size_t need = (c < L ? c : L) + 1;
                    trial(soff, doff, len, need,     c, "exact fit");
                    trial(soff, doff, len, need + 1, c, "one to spare");
                    if (need >= 2) trial(soff, doff, len, need - 1, c, "short by one");
                    trial(soff, doff, len, 120,      c, "roomy");
                    trial(soff, doff, len, 1,        c, "size 1");
                }
                trial(soff, doff, len, 120, 0, "count 0");
                /* _TRUNCATE at every size around the length: the STRUNCATE boundary */
                trial(soff, doff, len, L + 1, (size_t)-1, "_TRUNCATE exact");
                trial(soff, doff, len, L + 2, (size_t)-1, "_TRUNCATE spare");
                if (L >= 1) trial(soff, doff, len, L, (size_t)-1, "_TRUNCATE short by one");
                if (L >= 2) trial(soff, doff, len, L / 2, (size_t)-1, "_TRUNCATE half");
                trial(soff, doff, len, 1, (size_t)-1, "_TRUNCATE size 1");
                trial(soff, doff, len, 120, (size_t)-1, "_TRUNCATE roomy");
            }

    /* ---- src ending at a page boundary, next page NOACCESS ------------------------------------ */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 200 && fails < 15; ++len)
        {
            wchar_t* s = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i) s[i] = (wchar_t)(L'a' + (i % 23));
            s[len] = 0;
            size_t sizes[3] = { (size_t)len + 1, 300, 100000 };
            size_t counts[3] = { (size_t)len, (size_t)len + 5, (size_t)-1 };
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b)
            {
                seed();
                int hs, ho, hr = 0, rs, ro, rr;
                hits = 0; rs = sys(dsys, sizes[a], s, counts[b]);            hs = hits;
                hits = 0; ro = wia_wcsncpy_s(dour, sizes[a], s, counts[b]);  ho = hits;
                rr = ref_wcsncpy_s(dref, sizes[a], s, counts[b], &hr);
                if (rs != ro || rs != rr || hs != ho || hs != hr
                    || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
                { ++fails; printf("FAIL src-guard len=%d size=%zu count=%zd rc %d/%d/%d\n",
                                  len, sizes[a], (ptrdiff_t)counts[b], rs, ro, rr); }
            }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* ---- dst ending at a page boundary: no write may land past `size` -------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[400];
        for (int size = 1; size <= 160 && fails < 15; ++size)
            for (int len = 0; len <= 200 && fails < 15; len += 11)
                for (int k = 0; k < 3; ++k)
                {
                    size_t count = (k == 0) ? (size_t)len : (k == 1 ? (size_t)(len + 5) : (size_t)-1);
                    wchar_t* d = (wchar_t*)(mem + si.dwPageSize - size * 2);
                    wchar_t* s = srcbuf;
                    for (int i = 0; i < len; ++i) s[i] = (wchar_t)(L'a' + (i % 23));
                    s[len] = 0;
                    wmemset(d, 0x5A5A, size);
                    hits = 0; int rs = sys(d, (size_t)size, s, count); int hs = hits;
                    wmemcpy(snap, d, size);
                    wmemset(d, 0x5A5A, size);
                    hits = 0; int ro = wia_wcsncpy_s(d, (size_t)size, s, count); int ho = hits;
                    if (rs != ro || hs != ho || memcmp(snap, d, (size_t)size * 2))
                    { ++fails; printf("FAIL dst-guard size=%d len=%d count=%zd rc %d/%d iph %d/%d\n",
                                      size, len, (ptrdiff_t)count, rs, ro, hs, ho); }
                }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!argsnull) { ++fails; printf("FAIL handler received non-NULL arguments\n"); }
    if (!fails)
        printf("CORRECTNESS: PASS (wcsncpy_s vs live + oracle: return code, handler-invocation count and every\n"
               "  destination byte, over 16 src alignments x 8 dst alignments x lengths 0..90 x counts within\n"
               "  +-2 of the length x 5 size bounds each, plus _TRUNCATE at six sizes around the length; all\n"
               "  four NULL/size-0/count-0 validation paths including the documented all-NULL no-op and the\n"
               "  NULL-src-with-count-0 case that must NOT call the handler; a src NOACCESS page-guard sweep\n"
               "  over 3 sizes x 3 counts at every length; and a dst page-guard sweep over every size 1..160)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
