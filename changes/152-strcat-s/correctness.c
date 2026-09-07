// changes/152-strcat-s/correctness.c
// Bit-exact fuzz of wia_strcat_s vs live ucrtbase!strcat_s + oracle. Each trial compares the errno
// return, the number of invalid-parameter-handler invocations, and every byte of a canary-filled
// destination -- the byte compare is what pins the two partial-write paths (an unterminated dst,
// which must write ONLY dst[0], and ERANGE, which appends first and empties afterwards).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

extern int wia_strcat_s(char* dst, size_t size, const char* src);
int ref_strcat_s(char* dst, size_t size, const char* src, int* iph);

typedef int  (__cdecl *fn)(char*, size_t, const char*);
typedef void (__cdecl *IPH)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
typedef IPH  (__cdecl *pSetIPH)(IPH);

static fn  sys;
static int hits = 0, argsnull = 1, fails = 0;
static void __cdecl myiph(const wchar_t* e, const wchar_t* f, const wchar_t* fi, unsigned l, uintptr_t r)
{   if (e || f || fi || l || r) argsnull = 0; ++hits; }

#define DSZ 512
static char dsys[DSZ], dour[DSZ], dref[DSZ];
static char srcbuf[512];

// prelen < 0 means "fill the whole buffer, never terminate it" (the dst-not-terminated path)
static void seed(int doff, int prelen)
{
    for (int i = 0; i < DSZ; ++i) dsys[i] = dour[i] = dref[i] = (char)(0xC0 + (i & 15));
    if (prelen < 0) {
        for (int i = doff; i < DSZ; ++i) dsys[i] = dour[i] = dref[i] = 'Z';
    } else {
        for (int i = 0; i < prelen; ++i) dsys[doff+i] = dour[doff+i] = dref[doff+i] = (char)('A' + (i % 26));
        dsys[doff+prelen] = dour[doff+prelen] = dref[doff+prelen] = 0;
    }
}

static void trial(int soff, int doff, int prelen, int srclen, size_t size, const char* what)
{
    if (fails >= 15) return;
    char* s = srcbuf + soff;
    for (int i = 0; i < srclen; ++i) s[i] = (char)('a' + (i % 23));
    s[srclen] = 0;
    seed(doff, prelen);
    int hs, ho, hr = 0, rs, ro, rr;
    hits = 0; rs = sys(dsys + doff, size, s);           hs = hits;
    hits = 0; ro = wia_strcat_s(dour + doff, size, s);  ho = hits;
    rr = ref_strcat_s(dref + doff, size, s, &hr);
    if (rs != ro || rs != rr || hs != ho || hs != hr
        || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
    {
        ++fails;
        printf("FAIL %s soff=%d doff=%d pre=%d srclen=%d size=%zu  rc %d/%d/%d  iph %d/%d/%d\n",
               what, soff, doff, prelen, srclen, size, rs, ro, rr, hs, ho, hr);
        for (int i = 0; i < DSZ; ++i)
            if (dsys[i] != dour[i]) { printf("  first byte diff at %d: sys=%02X ours=%02X ref=%02X\n",
                                            i, (unsigned char)dsys[i], (unsigned char)dour[i], (unsigned char)dref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "strcat_s");
    pSetIPH set = (pSetIPH)GetProcAddress(u, "_set_invalid_parameter_handler");
    if (!sys || !set) { printf("no strcat_s / handler setter\n"); return 2; }
    set(myiph);

    /* /MD build on purpose -- see 150's RESULTS.md on the two copies of the handler state. */
    { hits = 0; _invalid_parameter_noinfo();
      if (hits != 1) { ++fails; printf("FAIL handler not reachable through _invalid_parameter_noinfo\n"); } }

    /* ---- NULL dst / size 0 / NULL src -------------------------------------------------------- */
    {
        int h1, h2, h3 = 0, r1, r2, r3;
        seed(0, 2);
        hits = 0; r1 = sys(0, 10, "abc");            h1 = hits;
        hits = 0; r2 = wia_strcat_s(0, 10, "abc");   h2 = hits;
        r3 = ref_strcat_s(0, 10, "abc", &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3 || memcmp(dsys, dour, DSZ))
        { ++fails; printf("FAIL NULL-dst rc %d/%d/%d iph %d/%d/%d\n", r1, r2, r3, h1, h2, h3); }
    }
    trial(0, 0, 2, 3, 0, "size-0");
    for (int pre = 0; pre < 6; ++pre)          /* NULL src, both with and without a terminated dst */
    {
        int h1, h2, h3 = 0, r1, r2, r3;
        seed(0, pre ? pre : -1);
        hits = 0; r1 = sys(dsys, 10, 0);             h1 = hits;
        hits = 0; r2 = wia_strcat_s(dour, 10, 0);    h2 = hits;
        r3 = ref_strcat_s(dref, 10, 0, &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3
            || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
        { ++fails; printf("FAIL NULL-src pre=%d rc %d/%d/%d iph %d/%d/%d\n", pre, r1, r2, r3, h1, h2, h3); }
    }

    /* ---- the grid ---------------------------------------------------------------------------- */
    for (int soff = 0; soff < 32 && fails < 15; ++soff)
        for (int doff = 0; doff < 8 && fails < 15; ++doff)
            for (int pre = 0; pre <= 70 && fails < 15; ++pre)
                for (int sl = 0; sl <= 70 && fails < 15; sl += (sl < 5 ? 1 : 11))
                {
                    size_t need = (size_t)pre + sl + 1;
                    trial(soff, doff, pre, sl, need,     "exact fit");
                    trial(soff, doff, pre, sl, need + 1, "one to spare");
                    trial(soff, doff, pre, sl, 200,      "roomy");
                    if (need >= 2) trial(soff, doff, pre, sl, need - 1, "short by one");
                    trial(soff, doff, pre, sl, (size_t)pre + 1, "room for dst only");
                    if (pre >= 1) trial(soff, doff, pre, sl, (size_t)pre, "dst itself truncated");
                    trial(soff, doff, pre, sl, 1, "size 1");
                }

    /* ---- dst with no terminator inside `size` ------------------------------------------------ */
    for (int doff = 0; doff < 8 && fails < 15; ++doff)
        for (int size = 1; size <= 140 && fails < 15; ++size)
            trial(0, doff, -1, 5, (size_t)size, "dst unterminated");

    /* ---- src ending at a page boundary, next page NOACCESS ----------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 150 && fails < 15; ++len)
        {
            char* s = mem + si.dwPageSize - (len + 1);
            for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
            s[len] = 0;
            size_t sizes[3] = { (size_t)len + 6, 300, 100000 };
            for (int k = 0; k < 3; ++k)
            {
                seed(0, 5);
                int hs, ho, hr = 0, rs, ro, rr;
                hits = 0; rs = sys(dsys, sizes[k], s);           hs = hits;
                hits = 0; ro = wia_strcat_s(dour, sizes[k], s);  ho = hits;
                rr = ref_strcat_s(dref, sizes[k], s, &hr);
                if (rs != ro || rs != rr || hs != ho || hs != hr
                    || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
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
        static char snap[400];
        for (int size = 1; size <= 200 && fails < 15; ++size)
            for (int pre = 0; pre < size && pre <= 60; pre += 3)
                for (int sl = 0; sl <= 90 && fails < 15; sl += 9)
                {
                    char* d = mem + si.dwPageSize - size;
                    char* s = srcbuf;
                    for (int i = 0; i < sl; ++i) s[i] = (char)('a' + (i % 23));
                    s[sl] = 0;
                    memset(d, 0x5A, size); for (int i = 0; i < pre; ++i) d[i] = 'A'; d[pre] = 0;
                    hits = 0; int rs = sys(d, (size_t)size, s); int hs = hits;
                    memcpy(snap, d, size);
                    memset(d, 0x5A, size); for (int i = 0; i < pre; ++i) d[i] = 'A'; d[pre] = 0;
                    hits = 0; int ro = wia_strcat_s(d, (size_t)size, s); int ho = hits;
                    if (rs != ro || hs != ho || memcmp(snap, d, size))
                    { ++fails; printf("FAIL dst-guard size=%d pre=%d sl=%d rc %d/%d iph %d/%d\n",
                                      size, pre, sl, rs, ro, hs, ho); }
                }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!argsnull) { ++fails; printf("FAIL handler received non-NULL arguments\n"); }
    if (!fails)
        printf("CORRECTNESS: PASS (strcat_s vs live + oracle: return code, handler-invocation count and every\n"
               "  destination byte, over 32 src alignments x 8 dst alignments x dst prefix 0..70 x src length\n"
               "  0..70 x 7 size bounds, the unterminated-dst path over every size 1..140, the NULL/size-0\n"
               "  paths, a src NOACCESS page-guard sweep, and a dst page-guard sweep over every size 1..200)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
