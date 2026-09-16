// changes/250-rtlipv6stringtoaddressexw/bench.c
// Gate 2: time wia_ip6exw against the live ntdll!RtlIpv6StringToAddressExW.
//
// THE CASE MIX. This function is an envelope over change 166, so the rows have to separate the part
// this change wrote from the part it linked:
//
//   * THE ADDRESS SHAPE drives change 166's core -- "::" alone, a full eight groups, an embedded
//     IPv4 tail, and the "::" compression shift, which is the one piece of real work in there;
//   * THE ENVELOPE FIELDS drive the six instructions that are new: brackets, a %scope of varying
//     length, and a :port at each of the three bases;
//   * AND THE FAILURE ROWS, which are not an afterthought. A parser is asked to reject far more
//     often than it is asked to accept, and the two costs are completely different: a refusal after
//     the address has already parsed is the whole envelope plus a wasted core call. An implementation
//     tuned only on valid input can regress on exactly the input a caller feeds it most.
//
// NOTHING TO RESTORE ANYWHERE. Every subject is read-only and each call writes only to its own
// 16-byte address, its ULONG and its USHORT -- so unlike changes 228, 230, 238, 245 and 247 there is
// no memcpy to charge to either side and no store-to-load hazard to place. The OUTPUTS still rotate
// across eight page-aligned slots, because an output written by one call and overwritten by the next
// is precisely the dependency that has skewed this project's benchmarks before.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern LONG wia_ip6exw(const wchar_t*, void*, ULONG*, USHORT*);
typedef LONG (NTAPI *FEXW)(const wchar_t*, void*, ULONG*, USHORT*);
static FEXW sys;

#define ROT 8

typedef struct { const wchar_t* s; unsigned char* a[ROT]; ULONG* sc[ROT]; USHORT* po[ROT];
                 unsigned idx; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)wia_ip6exw(k->s, k->a[i], k->sc[i], k->po[i]) + k->a[i][0];
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)sys(k->s, k->a[i], k->sc[i], k->po[i]) + k->a[i][0];
}
#pragma optimize("", on)

/* PAGE-ALIGNED ARENAS. Change 245's first benchmark cut its subjects out of .bss at whatever offsets
   the build produced and was not reproducible -- one row read 2371, 2378 and then 289 ns for the
   same call. Same lesson as changes 142, 228, 230 and 241, where a buffer's ADDRESS rather than its
   contents decided the verdict. */
static unsigned char* pool;
static unsigned long cur;
#define SLOT 4096

int main(void){
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    sys = (FEXW)GetProcAddress(h, "RtlIpv6StringToAddressExW");
    if (!sys) { printf("cannot resolve RtlIpv6StringToAddressExW\n"); return 1; }
    pool = (unsigned char*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!pool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 14 };
    static const wchar_t* S[N] = {
        L"::",
        L"::1",
        L"1:2:3:4:5:6:7:8",
        L"::ffff:1.2.3.4",
        L"[::1]",
        L"[::1]:80",
        L"[::1]:65535",
        L"[::1]:0x1f90",
        L"[::1]:010",
        L"::1%3",
        L"[fe80::1%4294967295]:443",
        L"[1:2:3:4:5:6:7:8%12345]:65535",
        L"[::1]:65536",
        L"[::1",
    };
    static const char* names[N] = {
        "::", "::1", "1:2:3:4:5:6:7:8", "::ffff:1.2.3.4", "[::1]", "[::1]:80",
        "[::1]:65535", "[::1]:0x1f90 (hex)", "[::1]:010 (octal)", "::1%3",
        "[fe80::1%4294967295]:443", "full + scope + port", "FAILS: port 65536",
        "FAILS: unclosed [",
    };
    static CASE C[N]; static wia_case cs[N];

    for (int i = 0; i < N; ++i) {
        C[i].s = S[i];
        C[i].idx = 0;
        /* STAGGERED WITHIN THE PAGE, NOT ALL AT OFFSET 0 -- and this was measured, not assumed. With
           a flat page per output, every one of the 24 outputs a row owns started at page offset 0,
           so every store in every row fell in the SAME L1 set: classic 4K aliasing. It showed up as
           rows that were slower than strictly harder rows -- "::" at 18.07 ns against "[::1]:80" at
           12.80, with change 166's core measured at 5.64 and 7.70 respectively, so the "envelope"
           appeared to cost 12.4 ns on the easy row and 5.2 on the hard one. Same code, less work,
           more time: that is a harness artefact, and reporting it as a 0.87x regression would have
           parked this change over the benchmark's own layout. Giving each slot its own offset
           spreads the 24 outputs across 24 sets. */
        for (int q = 0; q < ROT; ++q) {
            unsigned long off = (unsigned long)q * 256;
            C[i].a[q]  = pool + cur + off;                    cur += SLOT;
            C[i].sc[q] = (ULONG*)(pool + cur + off + 64);     cur += SLOT;
            C[i].po[q] = (USHORT*)(pool + cur + off + 128);   cur += SLOT;
        }
        if (cur > 3800000) { printf("BENCH SETUP ERROR: pool too small\n"); return 1; }
        cs[i].label = names[i];
        cs[i].bytes = wcslen(S[i]) * sizeof(wchar_t);
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }

    /* PER-ROW DIAGNOSTIC. Every row states what it asked for and what came back -- a benchmark row
       whose shape is not what its label says is the most expensive mistake this project makes, and
       the FAILURE rows are exactly where a label can quietly stop being true. */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each:\n");
        for (int i = 0; i < N; ++i) {
            ULONG sc = 0xDEADBEEF; USHORT po = 0xBEEF;
            unsigned char a[16];
            LONG st;
            memset(a, 0xCD, 16);
            st = wia_ip6exw(S[i], a, &sc, &po);
            printf("  %-28s st=%08lX addr=", names[i], (unsigned long)st);
            for (int q = 0; q < 8; ++q) printf("%02X", a[q]);
            printf(".. scope=%08lX port=%04X", (unsigned long)sc, po);
            {
                double t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
                printf("  ours(60) %8.2f ns\n", t);
            }
        }
        printf("\n");
    }

    /* WARM EVERY ROW'S WHOLE ROTATION, BOTH SIDES, BEFORE THE TABLE -- and this is not superstition,
       it was measured. The rows are timed in order, and each one owns 24 pages of arena (a 16-byte
       address, a ULONG and a USHORT x eight rotating slots). The per-row diagnostic above touches
       only slot 0 of each, so at table time the FIRST row's pages are both cold and the longest
       evicted: "::" came out at 18.31 ns while "[::1]:65535" -- which parses strictly more -- came
       out at 13.75, and the same "::" row measured 14.51 in the diagnostic that ran moments earlier.
       A row that is slower than a strictly harder row is a measurement artefact, not a cost, and
       reporting it as a 0.83x regression would have been reporting the harness. Same family of
       mistake as change 245's .bss placement, where a buffer's ADDRESS decided the verdict. */
    {
        volatile uint64_t sink = 0;
        for (int w = 0; w < 200; ++w)
            for (int i = 0; i < N; ++i) {
                sink += cs[i].ours(cs[i].ctx);
                sink += cs[i].system(cs[i].ctx);
            }
    }

    /* DOES THE SLOW NUMBER FOLLOW THE ROW OR THE POSITION? "::" reported 18.15 ns while "[::1]:80",
       which parses strictly more, reported 12.80 -- so one of the two is measuring something that is
       not the function. This block times the same three rows in three different orders. If a row's
       number moves when only its POSITION moves, the table is reporting its own layout. */
    {
        volatile uint64_t sink = 0;
        int order_a[3] = { 0, 1, 5 }, order_b[3] = { 5, 1, 0 }, order_c[3] = { 1, 5, 0 };
        int* ord[3]; int o, j;
        ord[0] = order_a; ord[1] = order_b; ord[2] = order_c;
        printf("the same three rows, timed in three different orders:\n");
        for (o = 0; o < 3; ++o) {
            printf("  order %d:", o + 1);
            for (j = 0; j < 3; ++j) {
                int i = ord[o][j];
                double t = wia_measure(cs[i].ours, cs[i].ctx, 120, &sink);
                printf("   %-12s %7.2f", names[i], t);
            }
            printf("\n");
        }
        printf("\n");
    }

    /* AND HOW MUCH OF EACH ROW IS THE CORE? Times change 166's wia_ip6w on exactly the substring
       this envelope hands it, in this same harness, so the two numbers are comparable and the
       envelope's own marginal cost is a subtraction rather than a guess. */
    {
        volatile uint64_t sink = 0;
        static const wchar_t* CS[4] = { L"::", L"::1", L"::1]", L"::1]:80" };
        static unsigned char ca[16];
        static const wchar_t* ct;
        int j;
        printf("change 166's core alone, in this harness, on what the envelope passes it:\n");
        for (j = 0; j < 4; ++j) {
            LARGE_INTEGER f, a, b;
            double best = 1e300;
            int t, q;
            QueryPerformanceFrequency(&f);
            for (t = 0; t < 120; ++t) {
                QueryPerformanceCounter(&a);
                for (q = 0; q < 20000; ++q) sink += wia_ip6w(CS[j], &ct, ca);
                QueryPerformanceCounter(&b);
                {
                    double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / 20000;
                    if (v < best) best = v;
                }
            }
            printf("  core(%-10ls) %7.2f ns\n", CS[j], best);
        }
        printf("\n");
    }

    return wia_bench_compare("ntdll RtlIpv6StringToAddressExW (wia = change 166's core behind a new "
                             "ASCII-only envelope; GB/s counts INPUT bytes)", cs, N, 300);
}
