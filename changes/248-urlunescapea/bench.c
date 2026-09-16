// changes/248-urlunescapea/bench.c
// Gate 2: time wia_urlunescapea against the live shlwapi!UrlUnescapeA.
//
// THE CASE MIX. This function's cost has four components and the rows separate them:
//
//   * the per-byte walk, which scales with the input;
//   * a FIXED HEAP TAX above 64 bytes. The shipped code stages every call through an inline buffer
//     whose size is written in the disassembly -- `mov dword ptr [rbp+7], 0x41` at 0x49E5F, so 65
//     BYTES -- and calls the grow helper at 0x0F730 past it. That step is at the SAME character count
//     as the wide form's, but at HALF the bytes, which is one reason the narrow form measured worse
//     per byte in the survey (1.85 ns/char against the wide form's 1.24). 63, 64, 65 and 100 are all
//     rows, so the step is visible rather than assumed;
//   * the escape density, because every escape is a scalar step on both sides while the runs between
//     them are 32-byte vector work here and a byte-at-a-time loop there;
//   * WHETHER THE CALLER'S BUFFER IS BIGGER THAN THE SOURCE. This is the one structural choice in the
//     change and it has to be measured, not asserted: unescaping never lengthens, so a destination
//     larger than the source cannot fail the size test, and -- because a zero-valued escape merely
//     ENDS the result here instead of refusing, unlike the wide form -- nothing has to be pre-scanned
//     either. That case is ONE pass. A caller that sizes its buffer to the result exactly pays for
//     two. Rows 10 and 11 are that caller, and they are the honest worst case for this change.
//     (A PLAIN source cannot reach the two-pass path at all: its result length equals its input
//     length, so an exact buffer is n+1, which is already bigger than n. Only escapes shrink the
//     result enough for an exact buffer to be smaller than the source, so rows 10 and 11 carry them.)
//
// NO RESTORE IS NEEDED ON THE NON-IN-PLACE ROWS: the destination is a separate buffer that is never
// read back, and the source is never modified -- correctness.c asserts that last part by comparing
// the whole buffer. The IN-PLACE rows are the exception and are the reason for the rotation: an
// in-place unescape consumes its own input, so it must be restored, and the restore is a memcpy of
// the whole string. That is heavy relative to the function, so it is (a) charged to BOTH sides
// equally, (b) placed on a buffer eight slots away from the one being processed, so it cannot stall
// the next call's wide load the way the restores that parked changes 142, 228, 230 and 241 did, and
// (c) printed on its own line so it can be subtracted.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

#define F_INPLACE     0x00100000u
#define F_EXTRA_INFO  0x02000000u

extern long wia_urlunescapea(char*, char*, unsigned long*, unsigned long);
typedef HRESULT (WINAPI *FN)(const char*, char*, DWORD*, DWORD);
static FN sys;

#define ROT 8

typedef struct { const char* s; DWORD cap; DWORD flags; char* d[ROT]; unsigned idx; } CASE;
typedef struct { const char* s; size_t n; DWORD flags; char* b[ROT]; unsigned idx; } IPCASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    DWORD cch = k->cap;
    return (uint64_t)(uint32_t)wia_urlunescapea((char*)k->s, k->d[i], &cch, k->flags);
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    DWORD cch = k->cap;
    return (uint64_t)(uint32_t)sys(k->s, k->d[i], &cch, k->flags);
}
static uint64_t op_ip_ours(void* c){
    IPCASE* k = (IPCASE*)c;
    unsigned i = k->idx, old = (i + 1) & (ROT - 1);
    k->idx = old;
    memcpy(k->b[old], k->s, k->n + 1);                   /* restore, ROT slots away */
    DWORD cch = 0;
    return (uint64_t)(uint32_t)wia_urlunescapea(k->b[i], 0, &cch, k->flags | F_INPLACE);
}
static uint64_t op_ip_sys(void* c){
    IPCASE* k = (IPCASE*)c;
    unsigned i = k->idx, old = (i + 1) & (ROT - 1);
    k->idx = old;
    memcpy(k->b[old], k->s, k->n + 1);
    DWORD cch = 0;
    return (uint64_t)(uint32_t)sys(k->b[i], 0, &cch, k->flags | F_INPLACE);
}
static uint64_t op_ip_restore(void* c){
    IPCASE* k = (IPCASE*)c;
    unsigned i = k->idx, old = (i + 1) & (ROT - 1);
    k->idx = old;
    memcpy(k->b[old], k->s, k->n + 1);
    return (uint64_t)(size_t)k->b[i];
}
#pragma optimize("", on)

/* PAGE-ALIGNED ARENAS, AND THIS IS NOT COSMETIC -- change 245's first benchmark cut its subjects out
   of .bss at whatever offsets the build produced and was NOT REPRODUCIBLE: one row read 2371, 2378
   and then 289 ns for the same call, and adding a diagnostic block ahead of the table (which moved
   nothing but the layout) shifted five other rows by up to 35%. Every subject and every destination
   here starts on a page boundary, so each row's source-to-destination relationship is fixed and
   identical from run to run. Same lesson as changes 142, 228, 230 and 241, where a buffer's ADDRESS
   rather than its contents decided the verdict. */
static char* spool;
static char* dpool;
static unsigned long scur, dcur;
#define SLOT 4096                     /* bytes per page-aligned slot */

static const char* mk(int n, int every)
{
    char* p = spool + scur;
    int k = 0;
    while (k < n) {
        if (every && (k % every) == 0 && k + 3 <= n) {
            p[k] = '%'; p[k+1] = '4'; p[k+2] = '1'; k += 3;
        } else {
            p[k] = (char)('a' + k % 26); ++k;
        }
    }
    p[n] = 0;
    scur += SLOT;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "UrlUnescapeA");
    if (!sys) { printf("cannot resolve UrlUnescapeA\n"); return 1; }
    spool = (char*)VirtualAlloc(0, 1 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    dpool = (char*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!spool || !dpool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 14 };
    /* EXACT = 1 means the capacity is the RESULT length plus one, which is the two-pass path */
    static const int LEN[N]    = { 16, 63, 64, 65, 100, 256, 1000, 1000, 1000, 1000, 1000, 1000, 16, 1000 };
    static const int EVERY[N]  = {  0,  0,  0,  0,   0,   0,    0,   12,    3,    0,   12,    3,  0,    0 };
    static const int EXACT[N]  = {  0,  0,  0,  0,   0,   0,    0,    0,    0,    0,    1,    1,  0,    0 };
    static const int IP[N]     = {  0,  0,  0,  0,   0,   0,    0,    0,    0,    0,    0,    0,  1,    1 };
    static const DWORD FLG[N]  = {  0,  0,  0,  0,   0,   0,    0,    0,    0, F_EXTRA_INFO, 0, 0, 0, 0 };
    static const char* names[] = {
        "16 plain", "63 plain (inline)", "64 plain (inline edge)", "65 plain (heap)", "100 plain",
        "256 plain", "1000 plain", "1000, 1 esc/12", "1000, all escapes", "1000 plain +extrainfo",
        "1000, 1 esc/12, exact cap", "1000, all esc, exact cap", "16 in place", "1000 in place" };
    static CASE C[N]; static IPCASE IPC[N]; static wia_case cs[N];

    for (int i = 0; i < N; ++i) {
        const char* s = mk(LEN[i], EVERY[i]);
        size_t n = strlen(s);
        if (IP[i]) {
            IPC[i].s = s; IPC[i].n = n; IPC[i].flags = FLG[i]; IPC[i].idx = 0;
            for (int q = 0; q < ROT; ++q) {
                IPC[i].b[q] = dpool + dcur; dcur += SLOT;
                memcpy(IPC[i].b[q], s, n + 1);
            }
            cs[i].ours = op_ip_ours; cs[i].system = op_ip_sys; cs[i].ctx = &IPC[i];
        } else {
            C[i].s = s; C[i].flags = FLG[i]; C[i].idx = 0;
            for (int q = 0; q < ROT; ++q) { C[i].d[q] = dpool + dcur; dcur += SLOT; }
            C[i].cap = (DWORD)n + 1;
            if (EXACT[i]) {                    /* ask what the result length is, then size to it */
                DWORD cch = (DWORD)n + 1;
                char* probe = dpool + dcur; dcur += SLOT;
                wia_urlunescapea((char*)s, probe, &cch, FLG[i]);
                C[i].cap = cch + 1;
            }
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
        if (dcur > 3500000 || scur > 900000) {
            printf("BENCH SETUP ERROR: pool too small (dcur=%lu scur=%lu)\n", dcur, scur);
            return 1;
        }
        cs[i].label = names[i];
        cs[i].bytes = n;
    }

    /* PER-ROW DIAGNOSTIC. Every row states what it actually asked for and what came back, because a
       benchmark row whose shape is not what its label says is the most expensive mistake this project
       makes: a subject whose escapable characters sat in the wrong URL segment made the discovery
       survey's UrlEscape row measure a no-op for a whole commit. The "pass" column is the one that
       matters here -- it says whether the row took the one-pass or the two-pass route. */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each:\n");
        for (int i = 0; i < N; ++i) {
            size_t n = IP[i] ? IPC[i].n : strlen(C[i].s);
            DWORD cch = IP[i] ? 0 : C[i].cap;
            long hr;
            if (IP[i]) {
                memcpy(IPC[i].b[0], IPC[i].s, n + 1);
                hr = wia_urlunescapea(IPC[i].b[0], 0, &cch, IPC[i].flags | F_INPLACE);
                memcpy(IPC[i].b[0], IPC[i].s, n + 1);
            } else {
                hr = wia_urlunescapea((char*)C[i].s, C[i].d[0], &cch, C[i].flags);
            }
            double t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
            printf("  %-27s inLen=%4llu cap=%5lu -> hr=%08lX cch=%5lu  %-8s ours(60) %9.2f ns\n",
                   names[i], (unsigned long long)n, (unsigned long)(IP[i] ? 0 : C[i].cap),
                   (unsigned long)hr, (unsigned long)cch,
                   IP[i] ? "in place" : (C[i].cap > (DWORD)n ? "1 pass" : "2 passes"), t);
        }
        printf("\n");
    }

    /* the in-place restore's own cost, so those two rows can be read honestly */
    {
        volatile uint64_t sink = 0;
        printf("the in-place rows carry a memcpy restore; its own cost, measured on this run:\n");
        for (int i = 0; i < N; ++i) {
            if (!IP[i]) continue;
            double r = wia_measure(op_ip_restore, &IPC[i], 60, &sink);
            printf("  %-27s restore alone %8.2f ns  (subtract from both sides of that row)\n",
                   names[i], r);
        }
        printf("\n");
    }

    return wia_bench_compare("shlwapi UrlUnescapeA (wia one AVX2 pass vs five scalar walks plus a "
                             "65-byte stage and a LocalAlloc; GB/s counts INPUT bytes)", cs, N, 300);
}
