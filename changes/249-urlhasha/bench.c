// changes/249-urlhasha/bench.c
// Gate 2: time wia_urlhasha against the live shlwapi!UrlHashA.
//
// The case mix. UrlHashA's cost is the product of two lengths and nothing else; there is no
// locale, no code page, no grammar and no allocation anywhere in it, so the rows separate the two:
//
//   * The url length drives the length scan (change 225's, a 32-byte AVX2 compare against the
//     shipped byte loop) and the number of source bytes the hash consumes;
//   * The digest size selects change 244's kernel. That is not a smooth curve and the rows are
//     placed where it steps: cbHash 1..4 take a LEAF path with exactly cbHash lanes and no saved
//     registers, cbHash >= 5 takes ceil(cbHash/12) passes of a twelve-lane kernel. So 4, 5, 12, 13
//     and 16 are all rows, 5 and 13 are the first case of each new pass, where a naive
//     "always twelve lanes" shape did its worst.
//
// And one row that is not about speed at all. "16 url, 1 digest" exists because change 244 measured
// its own twelve-lane kernel at 0.72x there before the leaf kernels were written: a pass costs the
// same whether it advances two lanes or twelve, so a one-byte digest done twelve lanes wide does
// eleven lanes of arithmetic for nothing. This change inherits that fix, and the row proves the
// inheritance rather than assuming it.
//
// No restore is needed anywhere here. The digest is a separate buffer that is never read back and
// the URL is never modified, correctness.c asserts that last part by comparing the whole buffer --
// so unlike changes 228, 230, 238 and 245 there is no memcpy to charge to either side and no
// store-to-load hazard to place. The digests still ROTATE across eight page-aligned slots, because a
// digest written by one call and overwritten by the next is exactly the dependency that made an
// earlier harness measure its own restore rather than the function.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern long wia_urlhasha(const char*, BYTE*, unsigned long);
typedef HRESULT (WINAPI *FA)(const char*, BYTE*, DWORD);
typedef HRESULT (WINAPI *FW)(const wchar_t*, BYTE*, DWORD);
typedef HRESULT (WINAPI *FH)(const BYTE*, DWORD, BYTE*, DWORD);
static FA sys;
static FW sysw;

#define ROT 8

typedef struct { const char* s; DWORD cb; BYTE* d[ROT]; unsigned idx; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)wia_urlhasha(k->s, k->d[i], k->cb) + k->d[i][0];
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)sys(k->s, k->d[i], k->cb) + k->d[i][0];
}
#pragma optimize("", on)

/* PAGE-ALIGNED ARENAS. Change 245's first benchmark cut its subjects out of .bss at whatever offsets
   the build produced and was not reproducible -- one row read 2371, 2378 and then 289 ns for the
   same call, and adding a diagnostic block ahead of the table, which moved nothing but the layout,
   shifted five other rows by up to 35%. Same lesson as changes 142, 228, 230 and 241, where a
   buffer's ADDRESS rather than its contents decided the verdict. */
static char* spool;
static BYTE* dpool;
static unsigned long scur, dcur;
/* 8192, not 4096: the longest subject here is 4096 Bytes plus its terminator, and at a 4096-byte
   slot that terminator landed in the next subject's slot and was overwritten by it. The per-row
   diagnostic caught it -- the row labelled "4096 url" reported urlLen=8192, having run off the
   end of its own string into the one after it -- which is the entire reason every row here
   prints what it actually asked for instead of what its label claims. */
#define SLOT 8192

static const char* mk(int n)
{
    char* p = spool + scur;
    int k;
    for (k = 0; k < n; ++k) p[k] = (char)('a' + (k * 7) % 26);
    p[n] = 0;
    scur += SLOT;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys  = (FA)GetProcAddress(h, "UrlHashA");
    sysw = (FW)GetProcAddress(h, "UrlHashW");
    if (!sys || !sysw) { printf("cannot resolve UrlHashA/W\n"); return 1; }
    spool = (char*)VirtualAlloc(0, 1 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    dpool = (BYTE*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!spool || !dpool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 14 };
    static const int   LEN[N] = { 16, 16, 16, 16, 16, 16, 64, 64, 256, 256, 1000, 1000, 4096, 4096 };
    static const DWORD CB [N] = {  1,  4,  5, 12, 13, 16,  1, 16,   1,  16,    1,   16,    1,   16 };
    static const char* names[] = {
        "16 url, 1 digest", "16 url, 4 digest", "16 url, 5 digest", "16 url, 12 digest",
        "16 url, 13 digest", "16 url, 16 digest", "64 url, 1 digest", "64 url, 16 digest",
        "256 url, 1 digest", "256 url, 16 digest", "1000 url, 1 digest", "1000 url, 16 digest",
        "4096 url, 1 digest", "4096 url, 16 digest" };
    static CASE C[N]; static wia_case cs[N];

    for (int i = 0; i < N; ++i) {
        C[i].s = mk(LEN[i]);
        C[i].cb = CB[i];
        C[i].idx = 0;
        for (int q = 0; q < ROT; ++q) { C[i].d[q] = dpool + dcur; dcur += SLOT; }
        if (dcur > 3500000 || scur > 900000) {
            printf("BENCH SETUP ERROR: pool too small\n"); return 1;
        }
        cs[i].label = names[i];
        /* GB/s counts source bytes x digest bytes, because that product is what the shipped loop
           actually costs: change 244's probes/cost.c measured the surface FLAT at 0.42 ns per pair
           for every digest of six bytes or more. A GB/s on source bytes alone would make the
           one-digest rows look twelve times better than the sixteen-digest ones for no reason. */
        cs[i].bytes = (size_t)LEN[i] * CB[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }

    /* PER-ROW DIAGNOSTIC. Every row states what it asked for and what came back, because a benchmark
       row whose shape is not what its label says is the most expensive mistake this project makes:
       a subject whose escapable characters sat in the wrong URL segment made the discovery survey's
       UrlEscape row measure a no-op for a whole commit. */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each  (digest shown to 8 bytes):\n");
        for (int i = 0; i < N; ++i) {
            long hr = wia_urlhasha(C[i].s, C[i].d[0], C[i].cb);
            printf("  %-22s urlLen=%4llu cbHash=%3lu -> hr=%08lX digest ", names[i],
                   (unsigned long long)strlen(C[i].s), (unsigned long)C[i].cb, (unsigned long)hr);
            for (DWORD q = 0; q < C[i].cb && q < 8; ++q) printf("%02X", C[i].d[0][q]);
            double t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
            printf("   ours(60) %9.2f ns\n", t);
        }
        printf("\n");
    }

    /* The wide export is the narrow one plus a conversion, and this block measures the part a patch
       on UrlHashA would and would not reach. kernelbase!UrlHashW builds a 65-byte inline narrow
       string and then does `call 0x12F750`, which IS UrlHashA -- so the hash is shared and the
       conversion is not. Printed rather than claimed, because it is the basis for the statement
       that one patch speeds up two exports. */
    {
        volatile uint64_t sink = 0;
        static wchar_t w[4200];
        static BYTE wd[64];
        printf("what a patch on the NARROW export does for the WIDE one:\n");
        for (int i = 0; i < N; i += 5) {
            size_t n = strlen(C[i].s);
            for (size_t k = 0; k <= n; ++k) w[k] = (wchar_t)(unsigned char)C[i].s[k];
            LARGE_INTEGER f, a, b;
            QueryPerformanceFrequency(&f);
            DWORD cb = C[i].cb;
            int inner = 20000;
            QueryPerformanceCounter(&a);
            for (int q = 0; q < inner; ++q) sink += sysw(w, wd, cb);
            QueryPerformanceCounter(&b);
            double wns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            double ans = wia_measure(cs[i].system, cs[i].ctx, 60, &sink);
            printf("  %-22s live UrlHashW %8.2f ns = live UrlHashA %8.2f + %7.2f ns of\n"
                   "  %-22s wide-to-narrow conversion, which this change does NOT replace\n",
                   names[i], wns, ans, wns - ans, "");
        }
        printf("\n");
    }

    /* Is the inlined worker really faster than the export? discovery/README.md recorded that the
       copy at 0xC0A10 measured "1.15-1.45x FASTER" than the HashData export at 0xBB750, and pulled
       this pair's projection down to ~1.6x geomean on the strength of it. That claim is testable
       from here: UrlHashA reaches the inlined copy and the HashData export is one call away, and the
       only difference between the two instruction streams is the export's two NULL checks and its
       trailing `xor eax, eax`. Subtract UrlHashA's own length scan -- measured on the same string --
       and what is left is the worker. Printed either way, because a projection this change already
       beat is worth correcting rather than quietly outrunning. */
    {
        volatile uint64_t sink = 0;
        static BYTE hdd[64];
        FH hdf = (FH)GetProcAddress(h, "HashData");
        printf("the inlined worker (via UrlHashA) against the HashData export, same bytes:\n");
        for (int i = 0; i < N; ++i) {
            if (CB[i] != 16 && CB[i] != 1) continue;
            size_t n = strlen(C[i].s);
            LARGE_INTEGER f, a, b;
            QueryPerformanceFrequency(&f);
            int inner = LEN[i] > 500 ? 2000 : 20000;
            double best_hd = 1e300, best_ua = 1e300;
            for (int t = 0; t < 40; ++t) {
                QueryPerformanceCounter(&a);
                for (int q = 0; q < inner; ++q) sink += hdf((const BYTE*)C[i].s, (DWORD)n, hdd, CB[i]);
                QueryPerformanceCounter(&b);
                double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
                if (v < best_hd) best_hd = v;
                QueryPerformanceCounter(&a);
                for (int q = 0; q < inner; ++q) sink += sys(C[i].s, hdd, CB[i]);
                QueryPerformanceCounter(&b);
                v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
                if (v < best_ua) best_ua = v;
            }
            printf("  %-22s HashData export %9.2f ns   UrlHashA (inlined copy + lstrlenA)"
                   " %9.2f ns\n", names[i], best_hd, best_ua);
        }
        printf("\n");
    }

    return wia_bench_compare("shlwapi UrlHashA (wia = change 225's scan + change 244's kernel behind "
                             "a six-instruction envelope; GB/s counts source x digest byte pairs)",
                             cs, N, 300);
}
