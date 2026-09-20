// changes/156-strncat-s/correctness.c
// Bit-exact fuzz of wia_strncat_s vs live ucrtbase!strncat_s + oracle. Each trial compares the errno
// return, the number of invalid-parameter-handler invocations, and every byte of a canary-filled
// destination, which is what separates the three partial-write paths (unterminated dst writes only
// dst[0]; ERANGE appends then empties; _TRUNCATE appends then terminates the buffer end).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

extern int wia_strncat_s(char* dst, size_t size, const char* src, size_t count);
int ref_strncat_s(char* dst, size_t size, const char* src, size_t count, int* iph);

typedef int  (__cdecl *fn)(char*, size_t, const char*, size_t);
typedef void (__cdecl *IPH)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
typedef IPH  (__cdecl *pSetIPH)(IPH);

static fn  sys;
static int hits = 0, argsnull = 1, fails = 0;
static void __cdecl myiph(const wchar_t* e, const wchar_t* f, const wchar_t* fi, unsigned l, uintptr_t r)
{   if (e || f || fi || l || r) argsnull = 0; ++hits; }

#define DSZ 512
static char dsys[DSZ], dour[DSZ], dref[DSZ];
static char srcbuf[512];

// prelen < 0 means "fill the whole buffer, never terminate it"
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

static void trial(int soff, int doff, int prelen, int srclen, size_t size, size_t count, const char* what)
{
    if (fails >= 15) return;
    char* s = srcbuf + soff;
    for (int i = 0; i < srclen; ++i) s[i] = (char)('a' + (i % 23));
    s[srclen] = 0;
    seed(doff, prelen);
    int hs, ho, hr = 0, rs, ro, rr;
    hits = 0; rs = sys(dsys + doff, size, s, count);            hs = hits;
    hits = 0; ro = wia_strncat_s(dour + doff, size, s, count);  ho = hits;
    rr = ref_strncat_s(dref + doff, size, s, count, &hr);
    if (rs != ro || rs != rr || hs != ho || hs != hr
        || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
    {
        ++fails;
        printf("FAIL %s soff=%d doff=%d pre=%d srclen=%d size=%zu count=%zd  rc %d/%d/%d  iph %d/%d/%d\n",
               what, soff, doff, prelen, srclen, size, (ptrdiff_t)count, rs, ro, rr, hs, ho, hr);
        for (int i = 0; i < DSZ; ++i)
            if (dsys[i] != dour[i]) { printf("  first byte diff at %d: sys=%02X ours=%02X ref=%02X\n",
                                            i, (unsigned char)dsys[i], (unsigned char)dour[i],
                                            (unsigned char)dref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "strncat_s");
    pSetIPH set = (pSetIPH)GetProcAddress(u, "_set_invalid_parameter_handler");
    if (!sys || !set) { printf("no strncat_s / handler setter\n"); return 2; }
    set(myiph);
    { hits = 0; _invalid_parameter_noinfo();
      if (hits != 1) { ++fails; printf("FAIL handler not reachable through _invalid_parameter_noinfo\n"); } }

    /* ---- the argument-validation paths ------------------------------------------------------- */
    {
        struct { int nulldst; size_t size; int nullsrc; size_t count; const char* tag; } A[] = {
            { 1, 10, 0, 3,  "NULL dst" },
            { 1,  0, 0, 0,  "NULL dst + size 0 + count 0 (documented no-op)" },
            { 1, 10, 0, 0,  "NULL dst + count 0, size != 0" },
            { 1,  0, 0, 3,  "NULL dst + size 0, count != 0" },
            { 0, 10, 1, 3,  "NULL src, count != 0" },
            { 0, 10, 1, 0,  "NULL src, count 0 -> writes NOTHING" },
        };
        for (int i = 0; i < 6; ++i)
        {
            seed(0, 2);
            char* ds = A[i].nulldst ? 0 : dsys;
            char* du = A[i].nulldst ? 0 : dour;
            char* dr = A[i].nulldst ? 0 : dref;
            const char* s = A[i].nullsrc ? 0 : "xyz";
            int h1, h2, h3 = 0, r1, r2, r3;
            hits = 0; r1 = sys(ds, A[i].size, s, A[i].count);            h1 = hits;
            hits = 0; r2 = wia_strncat_s(du, A[i].size, s, A[i].count);  h2 = hits;
            r3 = ref_strncat_s(dr, A[i].size, s, A[i].count, &h3);
            if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3
                || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
            { ++fails; printf("FAIL %s rc %d/%d/%d iph %d/%d/%d\n", A[i].tag, r1, r2, r3, h1, h2, h3); }
        }
    }
    trial(0, 0, 2, 3, 0, 3, "size 0");
    trial(0, 0, 2, 3, 0, 0, "size 0, count 0");
    /* an unterminated dst reached with count 0 and a VALID src must still be EINVAL */
    trial(0, 0, -1, 3, 5, 0, "unterminated dst, count 0, valid src");

    /* ---- the grid ---------------------------------------------------------------------------- */
    for (int soff = 0; soff < 32 && fails < 15; ++soff)
        for (int doff = 0; doff < 8 && fails < 15; ++doff)
            for (int pre = 0; pre <= 40 && fails < 15; ++pre)
                for (int sl = 0; sl <= 40 && fails < 15; sl += (sl < 4 ? 1 : 9))
                {
                    for (int dc = -2; dc <= 2; ++dc)
                    {
                        if (sl + dc < 0) continue;
                        size_t c = (size_t)(sl + dc);
                        size_t n = (c < (size_t)sl ? c : (size_t)sl);
                        size_t need = (size_t)pre + n + 1;
                        trial(soff, doff, pre, sl, need,     c, "exact fit");
                        trial(soff, doff, pre, sl, need + 1, c, "one to spare");
                        if (need >= 2) trial(soff, doff, pre, sl, need - 1, c, "short by one");
                        trial(soff, doff, pre, sl, 120,      c, "roomy");
                        trial(soff, doff, pre, sl, (size_t)pre + 1, c, "room for dst only");
                        if (pre >= 1) trial(soff, doff, pre, sl, (size_t)pre, c, "dst itself truncated");
                    }
                    trial(soff, doff, pre, sl, 120, 0, "count 0");
                    /* _TRUNCATE across the whole boundary */
                    trial(soff, doff, pre, sl, (size_t)pre + sl + 1, (size_t)-1, "_TRUNCATE exact");
                    trial(soff, doff, pre, sl, (size_t)pre + sl + 2, (size_t)-1, "_TRUNCATE spare");
                    if (pre + sl >= 1) trial(soff, doff, pre, sl, (size_t)pre + sl, (size_t)-1, "_TRUNCATE short by one");
                    trial(soff, doff, pre, sl, (size_t)pre + 1, (size_t)-1, "_TRUNCATE room for dst only");
                    if (pre >= 1) trial(soff, doff, pre, sl, (size_t)pre, (size_t)-1, "_TRUNCATE dst truncated");
                    trial(soff, doff, pre, sl, 120, (size_t)-1, "_TRUNCATE roomy");
                }

    /* ---- dst with no terminator inside `size` ------------------------------------------------ */
    for (int doff = 0; doff < 8 && fails < 15; ++doff)
        for (int size = 1; size <= 140 && fails < 15; ++size)
        {
            trial(0, doff, -1, 5, (size_t)size, 3, "dst unterminated");
            trial(0, doff, -1, 5, (size_t)size, (size_t)-1, "dst unterminated, _TRUNCATE");
        }

    /* ---- src ending at a page boundary -------------------------------------------------------- */
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
            size_t counts[3] = { (size_t)len, (size_t)len + 5, (size_t)-1 };
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b)
            {
                seed(0, 5);
                int hs, ho, hr = 0, rs, ro, rr;
                hits = 0; rs = sys(dsys, sizes[a], s, counts[b]);            hs = hits;
                hits = 0; ro = wia_strncat_s(dour, sizes[a], s, counts[b]);  ho = hits;
                rr = ref_strncat_s(dref, sizes[a], s, counts[b], &hr);
                if (rs != ro || rs != rr || hs != ho || hs != hr
                    || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
                { ++fails; printf("FAIL src-guard len=%d size=%zu count=%zd rc %d/%d/%d\n",
                                  len, sizes[a], (ptrdiff_t)counts[b], rs, ro, rr); }
            }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* ---- dst ending at a page boundary: neither the walk nor the append may cross ------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static char snap[400];
        for (int size = 1; size <= 140 && fails < 15; ++size)
            for (int pre = 0; pre < size && pre <= 40; pre += 3)
                for (int sl = 0; sl <= 60 && fails < 15; sl += 7)
                    for (int k = 0; k < 3; ++k)
                    {
                        size_t count = (k == 0) ? (size_t)sl : (k == 1 ? (size_t)(sl + 5) : (size_t)-1);
                        char* d = mem + si.dwPageSize - size;
                        char* s = srcbuf;
                        for (int i = 0; i < sl; ++i) s[i] = (char)('a' + (i % 23));
                        s[sl] = 0;
                        memset(d, 0x5A, size); for (int i = 0; i < pre; ++i) d[i] = 'A'; d[pre] = 0;
                        hits = 0; int rs = sys(d, (size_t)size, s, count); int hs = hits;
                        memcpy(snap, d, size);
                        memset(d, 0x5A, size); for (int i = 0; i < pre; ++i) d[i] = 'A'; d[pre] = 0;
                        hits = 0; int ro = wia_strncat_s(d, (size_t)size, s, count); int ho = hits;
                        if (rs != ro || hs != ho || memcmp(snap, d, size))
                        { ++fails; printf("FAIL dst-guard size=%d pre=%d sl=%d count=%zd rc %d/%d iph %d/%d\n",
                                          size, pre, sl, (ptrdiff_t)count, rs, ro, hs, ho); }
                    }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!argsnull) { ++fails; printf("FAIL handler received non-NULL arguments\n"); }
    if (!fails)
        printf("CORRECTNESS: PASS (strncat_s vs live + oracle: return code, handler-invocation count and every\n"
               "  destination byte, over 32 src alignments x 8 dst alignments x dst prefix 0..40 x src length\n"
               "  0..40 x counts within +-2 of the source length x 6 size bounds each, plus _TRUNCATE at six\n"
               "  sizes; all six NULL/size-0/count-0 validation paths including the all-NULL no-op and the\n"
               "  NULL-src-with-count-0 case that must write NOTHING; the unterminated-dst path over every\n"
               "  size 1..140 for both a counted and a _TRUNCATE call, and with count 0 and a valid src; a src\n"
               "  NOACCESS page-guard sweep over 3 sizes x 3 counts at every length; and a dst page-guard\n"
               "  sweep over every size 1..140 x 3 count shapes)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
