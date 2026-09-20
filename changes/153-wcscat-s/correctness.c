// changes/153-wcscat-s/correctness.c
// Bit-exact fuzz of wia_wcscat_s vs live ucrtbase!wcscat_s + oracle. Each trial compares the errno
// return, the number of invalid-parameter-handler invocations, and every BYTE of a canary-filled
// destination, which pins both partial-write paths (an unterminated dst, which must write only
// dst[0], and ERANGE, which appends first and empties afterwards).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>

extern int wia_wcscat_s(wchar_t* dst, size_t size, const wchar_t* src);
int ref_wcscat_s(wchar_t* dst, size_t size, const wchar_t* src, int* iph);

typedef int  (__cdecl *fn)(wchar_t*, size_t, const wchar_t*);
typedef void (__cdecl *IPH)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
typedef IPH  (__cdecl *pSetIPH)(IPH);

static fn  sys;
static int hits = 0, argsnull = 1, fails = 0;
static void __cdecl myiph(const wchar_t* e, const wchar_t* f, const wchar_t* fi, unsigned l, uintptr_t r)
{   if (e || f || fi || l || r) argsnull = 0; ++hits; }

#define DW 512
static wchar_t dsys[DW], dour[DW], dref[DW];
static wchar_t srcbuf[512];

// prelen < 0 means "fill the whole buffer, never terminate it"
static void seed(int doff, int prelen)
{
    for (int i = 0; i < DW; ++i) dsys[i] = dour[i] = dref[i] = (wchar_t)(0xC0C0 + (i & 15));
    if (prelen < 0) {
        for (int i = doff; i < DW; ++i) dsys[i] = dour[i] = dref[i] = L'Z';
    } else {
        for (int i = 0; i < prelen; ++i)
            dsys[doff+i] = dour[doff+i] = dref[doff+i] = (wchar_t)(0x4100 + (i % 26));  /* low byte 0 */
        dsys[doff+prelen] = dour[doff+prelen] = dref[doff+prelen] = 0;
    }
}

static void trial(int soff, int doff, int prelen, int srclen, size_t size, wchar_t base, const char* what)
{
    if (fails >= 15) return;
    wchar_t* s = srcbuf + soff;
    for (int i = 0; i < srclen; ++i) s[i] = (wchar_t)(base + (i % 23));
    s[srclen] = 0;
    seed(doff, prelen);
    int hs, ho, hr = 0, rs, ro, rr;
    hits = 0; rs = sys(dsys + doff, size, s);            hs = hits;
    hits = 0; ro = wia_wcscat_s(dour + doff, size, s);   ho = hits;
    rr = ref_wcscat_s(dref + doff, size, s, &hr);
    if (rs != ro || rs != rr || hs != ho || hs != hr
        || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
    {
        ++fails;
        printf("FAIL %s soff=%d doff=%d pre=%d srclen=%d size=%zu  rc %d/%d/%d  iph %d/%d/%d\n",
               what, soff, doff, prelen, srclen, size, rs, ro, rr, hs, ho, hr);
        for (int i = 0; i < DW; ++i)
            if (dsys[i] != dour[i]) { printf("  first wchar diff at %d: sys=%04X ours=%04X ref=%04X\n",
                                            i, dsys[i], dour[i], dref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "wcscat_s");
    pSetIPH set = (pSetIPH)GetProcAddress(u, "_set_invalid_parameter_handler");
    if (!sys || !set) { printf("no wcscat_s / handler setter\n"); return 2; }
    set(myiph);

    { hits = 0; _invalid_parameter_noinfo();
      if (hits != 1) { ++fails; printf("FAIL handler not reachable through _invalid_parameter_noinfo\n"); } }

    /* ---- NULL dst / size 0 / NULL src -------------------------------------------------------- */
    {
        int h1, h2, h3 = 0, r1, r2, r3;
        seed(0, 2);
        hits = 0; r1 = sys(0, 10, L"abc");            h1 = hits;
        hits = 0; r2 = wia_wcscat_s(0, 10, L"abc");   h2 = hits;
        r3 = ref_wcscat_s(0, 10, L"abc", &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3 || memcmp(dsys, dour, sizeof dsys))
        { ++fails; printf("FAIL NULL-dst rc %d/%d/%d iph %d/%d/%d\n", r1, r2, r3, h1, h2, h3); }
    }
    trial(0, 0, 2, 3, 0, L'a', "size-0");
    for (int pre = 0; pre < 6; ++pre)         /* NULL src, with a terminated AND an unterminated dst */
    {
        int h1, h2, h3 = 0, r1, r2, r3;
        seed(0, pre ? pre : -1);
        hits = 0; r1 = sys(dsys, 10, 0);              h1 = hits;
        hits = 0; r2 = wia_wcscat_s(dour, 10, 0);     h2 = hits;
        r3 = ref_wcscat_s(dref, 10, 0, &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3
            || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
        { ++fails; printf("FAIL NULL-src pre=%d rc %d/%d/%d iph %d/%d/%d\n", pre, r1, r2, r3, h1, h2, h3); }
    }

    /* ---- the grid; the fill alternates between a zero high byte and a zero low byte, either of
       which a byte-granular terminator scan would false-hit -------------------------------------- */
    for (int soff = 0; soff < 16 && fails < 15; ++soff)
        for (int doff = 0; doff < 8 && fails < 15; ++doff)
            for (int pre = 0; pre <= 70 && fails < 15; ++pre)
                for (int sl = 0; sl <= 70 && fails < 15; sl += (sl < 5 ? 1 : 11))
                {
                    wchar_t base = (pre & 1) ? (wchar_t)0x4100 : (wchar_t)0x0041;
                    size_t need = (size_t)pre + sl + 1;
                    trial(soff, doff, pre, sl, need,     base, "exact fit");
                    trial(soff, doff, pre, sl, need + 1, base, "one to spare");
                    trial(soff, doff, pre, sl, 200,      base, "roomy");
                    if (need >= 2) trial(soff, doff, pre, sl, need - 1, base, "short by one");
                    trial(soff, doff, pre, sl, (size_t)pre + 1, base, "room for dst only");
                    if (pre >= 1) trial(soff, doff, pre, sl, (size_t)pre, base, "dst itself truncated");
                    trial(soff, doff, pre, sl, 1, base, "size 1");
                }

    /* ---- dst with no terminator inside `size` ------------------------------------------------ */
    for (int doff = 0; doff < 8 && fails < 15; ++doff)
        for (int size = 1; size <= 140 && fails < 15; ++size)
            trial(0, doff, -1, 5, (size_t)size, L'a', "dst unterminated");

    /* ---- src ending at a page boundary, next page NOACCESS ----------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 150 && fails < 15; ++len)
        {
            wchar_t* s = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i) s[i] = (wchar_t)(L'a' + (i % 23));
            s[len] = 0;
            size_t sizes[3] = { (size_t)len + 6, 300, 100000 };
            for (int k = 0; k < 3; ++k)
            {
                seed(0, 5);
                int hs, ho, hr = 0, rs, ro, rr;
                hits = 0; rs = sys(dsys, sizes[k], s);           hs = hits;
                hits = 0; ro = wia_wcscat_s(dour, sizes[k], s);  ho = hits;
                rr = ref_wcscat_s(dref, sizes[k], s, &hr);
                if (rs != ro || rs != rr || hs != ho || hs != hr
                    || memcmp(dsys, dour, sizeof dsys) || memcmp(dsys, dref, sizeof dsys))
                { ++fails; printf("FAIL src-guard len=%d size=%zu rc %d/%d/%d\n", len, sizes[k], rs, ro, rr); }
            }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* ---- dst ending at a page boundary: neither the dst scan nor the append may cross --------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[256];
        for (int size = 1; size <= 200 && fails < 15; ++size)
            for (int pre = 0; pre < size && pre <= 60; pre += 3)
                for (int sl = 0; sl <= 90 && fails < 15; sl += 9)
                {
                    wchar_t* d = (wchar_t*)(mem + si.dwPageSize - size * 2);
                    for (int i = 0; i < sl; ++i) srcbuf[i] = (wchar_t)(L'a' + (i % 23));
                    srcbuf[sl] = 0;
                    wmemset(d, 0x5A5A, size); for (int i = 0; i < pre; ++i) d[i] = L'A'; d[pre] = 0;
                    hits = 0; int rs = sys(d, (size_t)size, srcbuf); int hs = hits;
                    wmemcpy(snap, d, size);
                    wmemset(d, 0x5A5A, size); for (int i = 0; i < pre; ++i) d[i] = L'A'; d[pre] = 0;
                    hits = 0; int ro = wia_wcscat_s(d, (size_t)size, srcbuf); int ho = hits;
                    if (rs != ro || hs != ho || memcmp(snap, d, (size_t)size * 2))
                    { ++fails; printf("FAIL dst-guard size=%d pre=%d sl=%d rc %d/%d iph %d/%d\n",
                                      size, pre, sl, rs, ro, hs, ho); }
                }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!argsnull) { ++fails; printf("FAIL handler received non-NULL arguments\n"); }
    if (!fails)
        printf("CORRECTNESS: PASS (wcscat_s vs live + oracle: return code, handler-invocation count and every\n"
               "  destination byte, over 16 src alignments x 8 dst alignments x dst prefix 0..70 x src length\n"
               "  0..70 x 7 size bounds, with zero-high-byte and zero-low-byte wchar fills, the\n"
               "  unterminated-dst path over every size 1..140, the NULL/size-0 paths, a src NOACCESS\n"
               "  page-guard sweep, and a dst page-guard sweep over every size 1..200)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
