// changes/150-strcpy-s/correctness.c
// Bit-exact fuzz of wia_strcpy_s vs live ucrtbase!strcpy_s + oracle. Every trial compares THREE
// things: the errno return, the number of invalid-parameter-handler invocations, and every byte of a
// canary-filled destination buffer -- the last one is what pins the ERANGE path, which writes `size`
// bytes of src before emptying dst.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

extern int wia_strcpy_s(char* dst, size_t size, const char* src);
int ref_strcpy_s(char* dst, size_t size, const char* src, int* iph);

typedef int  (__cdecl *fn)(char*, size_t, const char*);
typedef void (__cdecl *IPH)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
typedef IPH  (__cdecl *pSetIPH)(IPH);

static fn  sys;
static int hits = 0, argsnull = 1, fails = 0;
static void __cdecl myiph(const wchar_t* e, const wchar_t* f, const wchar_t* fi, unsigned l, uintptr_t r)
{   if (e || f || fi || l || r) argsnull = 0; ++hits; }

#define DSZ 320
static char dsys[DSZ], dour[DSZ], dref[DSZ];
static char srcbuf[512];

static void seed(void)
{   for (int i = 0; i < DSZ; ++i) dsys[i] = dour[i] = dref[i] = (char)(0xC0 + (i & 15)); }

static void trial(int soff, int doff, int len, size_t size, const char* what)
{
    if (fails >= 15) return;
    char* s = srcbuf + soff;
    for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
    s[len] = 0;
    seed();
    int hs, ho, hr, rs, ro, rr;
    hits = 0; rs = sys(dsys + doff, size, s);            hs = hits;
    hits = 0; ro = wia_strcpy_s(dour + doff, size, s);   ho = hits;
    hr = 0;   rr = ref_strcpy_s(dref + doff, size, s, &hr);
    if (rs != ro || rs != rr || hs != ho || hs != hr
        || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
    {
        ++fails;
        printf("FAIL %s soff=%d doff=%d len=%d size=%zu  rc sys=%d ours=%d ref=%d  iph sys=%d ours=%d ref=%d\n",
               what, soff, doff, len, size, rs, ro, rr, hs, ho, hr);
        for (int i = 0; i < DSZ && i < 40; ++i)
            if (dsys[i] != dour[i]) { printf("  first byte diff at %d: sys=%02X ours=%02X ref=%02X\n",
                                            i, (unsigned char)dsys[i], (unsigned char)dour[i], (unsigned char)dref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);    /* the failure modes here are process-killing, so never buffer */
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "strcpy_s");
    pSetIPH set = (pSetIPH)GetProcAddress(u, "_set_invalid_parameter_handler");
    if (!sys || !set) { printf("no strcpy_s / handler setter\n"); return 2; }
    set(myiph);                       /* without this the live ERANGE path __fastfails the process */
    /* This build is /MD on purpose: with the default /MT the app would carry its OWN copy of the
       handler state, so ucrtbase's strcpy_s and our `_invalid_parameter_noinfo` would consult two
       different handlers and the comparison would be meaningless (the static one is unset, so it
       __fastfails). /MD puts both on ucrtbase's state, which is what a real caller sees. */
    { hits = 0; _invalid_parameter_noinfo();
      if (hits != 1) { ++fails; printf("FAIL handler not reachable through _invalid_parameter_noinfo\n"); } }

    /* ---- NULL / zero-size argument paths (dst must not be touched for the first two) ---------- */
    {
        int h1 = 0, h2 = 0, h3 = 0, r1, r2, r3;
        seed();
        hits = 0; r1 = sys(0, 10, "abc");           h1 = hits;
        hits = 0; r2 = wia_strcpy_s(0, 10, "abc");  h2 = hits;
        r3 = ref_strcpy_s(0, 10, "abc", &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3 || memcmp(dsys, dour, DSZ))
        { ++fails; printf("FAIL NULL-dst rc %d/%d/%d iph %d/%d/%d\n", r1, r2, r3, h1, h2, h3); }
    }
    trial(0, 0, 3, 0, "size-0");                      /* EINVAL, dst untouched */
    {
        int h1 = 0, h2 = 0, h3 = 0, r1, r2, r3;
        seed();
        hits = 0; r1 = sys(dsys, 10, 0);              h1 = hits;
        hits = 0; r2 = wia_strcpy_s(dour, 10, 0);     h2 = hits;
        r3 = ref_strcpy_s(dref, 10, 0, &h3);
        if (r1 != r2 || r1 != r3 || h1 != h2 || h1 != h3
            || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
        { ++fails; printf("FAIL NULL-src rc %d/%d/%d iph %d/%d/%d\n", r1, r2, r3, h1, h2, h3); }
    }

    /* ---- the grid: src/dst alignment x length x every interesting bound ----------------------- */
    for (int soff = 0; soff < 32 && fails < 15; ++soff)
        for (int doff = 0; doff < 8 && fails < 15; ++doff)
            for (int len = 0; len <= 140 && fails < 15; ++len)
            {
                trial(soff, doff, len, (size_t)len + 1, "exact fit");
                trial(soff, doff, len, (size_t)len + 2, "one to spare");
                trial(soff, doff, len, 200,             "roomy");
                if (len >= 1) trial(soff, doff, len, (size_t)len,       "short by one");
                if (len >= 2) trial(soff, doff, len, (size_t)len / 2,   "half");
                trial(soff, doff, len, 1,               "size 1");
                trial(soff, doff, len, 2,               "size 2");
            }

    /* ---- src ending at a page boundary, next page NOACCESS ----------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 200 && fails < 15; ++len)
        {
            char* s = mem + si.dwPageSize - (len + 1);     /* terminator is the page's last byte */
            for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
            s[len] = 0;
            size_t sizes[4] = { (size_t)len + 1, 300, 100000, (size_t)(len ? len : 1) };
            for (int k = 0; k < 4; ++k)
            {
                if (sizes[k] > (size_t)len + 1 && sizes[k] > 300) { /* huge size, NUL still found */ }
                if (k == 3 && len == 0) continue;                  /* size=len would be 0 */
                seed();
                int hs, ho, hr = 0, rs, ro, rr;
                hits = 0; rs = sys(dsys, sizes[k], s);            hs = hits;
                hits = 0; ro = wia_strcpy_s(dour, sizes[k], s);   ho = hits;
                rr = ref_strcpy_s(dref, sizes[k], s, &hr);
                if (rs != ro || rs != rr || hs != ho || hs != hr
                    || memcmp(dsys, dour, DSZ) || memcmp(dsys, dref, DSZ))
                { ++fails; printf("FAIL src-guard len=%d size=%zu rc %d/%d/%d iph %d/%d/%d\n",
                                  len, sizes[k], rs, ro, rr, hs, ho, hr); }
            }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* ---- dst ending at a page boundary: writing one byte past `size` must fault --------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static char snap[400];
        for (int size = 1; size <= 200 && fails < 15; ++size)
            for (int len = 0; len <= 200 && fails < 15; len += 7)
            {
                char* d = mem + si.dwPageSize - size;    /* last writable byte is the page's last */
                char* s = srcbuf;
                for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
                s[len] = 0;
                memset(d, 0x5A, size);
                hits = 0; int rs = sys(d, (size_t)size, s); int hs = hits;
                memcpy(snap, d, size);
                memset(d, 0x5A, size);
                hits = 0; int ro = wia_strcpy_s(d, (size_t)size, s); int ho = hits;
                if (rs != ro || hs != ho || memcmp(snap, d, size))
                { ++fails; printf("FAIL dst-guard size=%d len=%d rc %d/%d iph %d/%d\n",
                                  size, len, rs, ro, hs, ho); }
            }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!argsnull) { ++fails; printf("FAIL handler received non-NULL arguments\n"); }
    if (!fails)
        printf("CORRECTNESS: PASS (strcpy_s vs live + oracle: return code, handler-invocation count and every\n"
               "  destination byte, over 32 src alignments x 8 dst alignments x lengths 0..140 x 7 size bounds,\n"
               "  the NULL-dst/NULL-src/size-0 paths, a src NOACCESS page-guard sweep, and a dst page-guard\n"
               "  sweep proving no write lands past `size`)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
