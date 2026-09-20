/* changes/296-rtlcopyunicodestring/bench.c -- wia_copyus vs the LIVE ntdll export.
 *
 * FOUR tables, and the change lands only if none of them reports a regression.
 *
 *   [1] the ordinary call, malloc'd buffers: the whole source fits, so n = src->Length and a wide
 *       NUL is written. This is the shape every one of the 31 live modules that bind this export
 *       makes, and the one discovery/ntdll_tier3.c timed (3.55 ns at 8 wchars, 12.50 at 254,
 *       113.30 at 4095 -- about 40 GB/s at 254 characters). Same allocation style as the rest of
 *       this tree, so the numbers are comparable with its neighbours.
 *
 *   [2] the TRUNCATING call, with dst->MaximumLength ODD and smaller than src->Length. That path
 *       copies MaximumLength bytes exactly -- an odd byte count -- and writes no NUL, so it lands
 *       on ladder arms case [1] never touches. A size class that exists only on a contract edge
 *       still has to not regress.
 *
 *   [3] and [4] exist because tables [1] and [2] CANNOT BE TRUSTED ON THEIR OWN, and finding that
 *       out was most of the work in this change. A wide copy's speed depends on two properties of
 *       the caller's buffers that malloc picks by accident:
 *
 *          dst & 31            -- a 32-byte store that is not 32-aligned splits a cache line on
 *                                 every other block. impl.asm fixes this by aligning its stores.
 *          (dst - src) % 4096  -- 4K aliasing. A load is stalled behind an in-flight store that
 *                                 shares its low 12 address bits. NOT fixable in software, and it
 *                                 costs a 32-byte loop more than ntdll's 16-byte one because the
 *                                 alias window is as wide as the access.
 *
 *       With one malloc'd buffer per class, a size class's verdict is decided by where that build
 *       happened to put the heap, and it is not even stable across rebuilds: six builds of this
 *       change measured the 128-wchar class, running identical code in five of them, at 5.00,
 *       6.14, 6.30, 6.34, 6.66 and 7.41 ns. So both axes are put IN the gate instead:
 *
 *       [3] sweeps the destination through a 64-byte line with BOTH buffers page-aligned, which
 *           forces (dst - src) % 4096 into [0,64) -- the aliasing regime, the adversarial one.
 *       [4] is the same sweep with the source moved to page offset 2048, which is the ordinary
 *           regime two unrelated allocations land in.
 *
 *       Each row holds its alignment FIXED for the whole timing loop, which is how every other
 *       bench in this repository measures and the only way the number means anything: mixing
 *       alignments inside one loop measures the microcode's reaction to the mixing. (It is a real
 *       effect -- rotating the destination offset every call costs `rep movsb` 3x -- but it is a
 *       different question from "is this faster for a caller", and it is recorded in RESULTS.md
 *       rather than smuggled into the gate.)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern void wia_copyus(US*, const US*);
typedef void (NTAPI *fn)(US*, const US*);
static fn sys;

typedef struct { wchar_t* buf; USHORT maxlen; US src; } ctx_t;
static uint64_t op_ours(void* c) { ctx_t* m = (ctx_t*)c; US u = { 0, m->maxlen, m->buf }; wia_copyus(&u, &m->src); return u.Length; }
static uint64_t op_sys (void* c) { ctx_t* m = (ctx_t*)c; US u = { 0, m->maxlen, m->buf }; sys(&u, &m->src);        return u.Length; }

/* ---- tables [1] and [2]: the house-standard shape ------------------------------------------ */
enum { K = 11 };
static const int         L[K] = { 1, 2, 4, 8, 16, 32, 64, 128, 254, 1024, 4095 };
static const char* const N[K] = { "1w", "2w", "4w", "8w", "16w", "32w", "64w", "128w", "254w", "1024w", "4095w" };
static ctx_t cx[K], cy[K];
static wia_case cs[K], ct[K];

/* ---- tables [3] and [4]: the alignment sweep ------------------------------------------------ */
enum { SN = 3, SO = 8, SK = SN * SO };
static const int         SL[SN] = { 254, 1024, 4095 };
static const char* const SLN[SN] = { "254w", "1024w", "4095w" };
static const int         SOFF[SO] = { 0, 8, 16, 24, 32, 40, 48, 56 };
static ctx_t  sx[SK], sy[SK];
static wia_case ss[SK], st[SK];
static char slab[SK][24], tlab[SK][24];

static unsigned char* page(size_t n)
{
    return (unsigned char*)VirtualAlloc(NULL, n + 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    sys = (fn)GetProcAddress(h, "RtlCopyUnicodeString");
    if (!sys) { printf("no ntdll!RtlCopyUnicodeString\n"); return 2; }

    for (int i = 0; i < K; ++i) {
        int n = L[i];
        wchar_t* s = (wchar_t*)malloc((size_t)(n + 1) * 2);
        for (int k = 0; k < n; ++k) s[k] = (wchar_t)(L'a' + (k % 26));
        s[n] = 0;

        cx[i].buf = (wchar_t*)malloc((size_t)(n + 8) * 2);
        cx[i].maxlen = (USHORT)(n * 2 + 4);
        cx[i].src.Length = (USHORT)(n * 2); cx[i].src.MaximumLength = (USHORT)(n * 2); cx[i].src.Buffer = s;
        cs[i].label = N[i]; cs[i].bytes = (size_t)n * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];

        cy[i].buf = (wchar_t*)malloc((size_t)(n + 8) * 2);
        cy[i].maxlen = (USHORT)(n * 2 - 1);
        if (n == 1) cy[i].maxlen = 1;
        cy[i].src.Length = (USHORT)(n * 2); cy[i].src.MaximumLength = (USHORT)(n * 2); cy[i].src.Buffer = s;
        ct[i].label = N[i]; ct[i].bytes = (size_t)cy[i].maxlen;
        ct[i].ours = op_ours; ct[i].system = op_sys; ct[i].ctx = &cy[i];
    }

    /* [3] both page-aligned  -> (dst-src) % 4096 = the offset itself, i.e. the aliasing regime.
       [4] source at page offset 2048 -> the distance stays ~2 KB from a multiple of 4096.       */
    for (int a = 0; a < SN; ++a) {
        int n = SL[a];
        unsigned char* sp3 = page((size_t)n * 2);
        unsigned char* sp4 = page((size_t)n * 2 + 4096) + 2048;
        unsigned char* dp3 = page((size_t)n * 2 + 256);
        unsigned char* dp4 = page((size_t)n * 2 + 256);
        for (int k = 0; k < n; ++k) {
            ((wchar_t*)sp3)[k] = (wchar_t)(L'a' + (k % 26));
            ((wchar_t*)sp4)[k] = (wchar_t)(L'a' + (k % 26));
        }
        for (int b = 0; b < SO; ++b) {
            int idx = a * SO + b;
            sprintf(slab[idx], "%s@%d", SLN[a], SOFF[b]);
            sprintf(tlab[idx], "%s@%d", SLN[a], SOFF[b]);

            sx[idx].buf = (wchar_t*)(dp3 + SOFF[b]);
            sx[idx].maxlen = (USHORT)(n * 2 + 4);
            sx[idx].src.Length = (USHORT)(n * 2); sx[idx].src.MaximumLength = (USHORT)(n * 2);
            sx[idx].src.Buffer = (wchar_t*)sp3;
            ss[idx].label = slab[idx]; ss[idx].bytes = (size_t)n * 2;
            ss[idx].ours = op_ours; ss[idx].system = op_sys; ss[idx].ctx = &sx[idx];

            sy[idx].buf = (wchar_t*)(dp4 + SOFF[b]);
            sy[idx].maxlen = (USHORT)(n * 2 + 4);
            sy[idx].src.Length = (USHORT)(n * 2); sy[idx].src.MaximumLength = (USHORT)(n * 2);
            sy[idx].src.Buffer = (wchar_t*)sp4;
            st[idx].label = tlab[idx]; st[idx].bytes = (size_t)n * 2;
            st[idx].ours = op_ours; st[idx].system = op_sys; st[idx].ctx = &sy[idx];
        }
    }

    int bad = 0;
    bad |= wia_bench_compare("[1] fits, NUL written, malloc'd buffers (wia frameless inline AVX2/ERMS vs ntdll frame+memmove call)", cs, K, 200);
    bad |= wia_bench_compare("[2] TRUNCATING, ODD MaximumLength, no NUL, malloc'd buffers", ct, K, 200);
    bad |= wia_bench_compare("[3] destination alignment sweep, BOTH buffers page-aligned -- (dst-src)%4096 in [0,64), the 4K-ALIASING regime", ss, SK, 120);
    bad |= wia_bench_compare("[4] destination alignment sweep, source at page offset 2048 -- the ordinary regime", st, SK, 120);
    return bad;
}
