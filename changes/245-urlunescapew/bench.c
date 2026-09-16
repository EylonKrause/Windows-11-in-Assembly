// changes/245-urlunescapew/bench.c
// Gate 2: time wia_urlunescapew against the live shlwapi!UrlUnescapeW.
//
// THE CASE MIX. This function's cost has three components and the rows are chosen to separate them:
//   * the per-character walk, which scales with the input;
//   * a FIXED heap tax above 64 wide characters, where the shipped code stops fitting its 65-WCHAR
//     stage buffer and calls LocalAlloc(LMEM_ZEROINIT) -- discovery measured the step directly,
//     1.648 ns/char at 64 characters against 2.067 at 100 -- so 63 and 64 are both rows;
//   * the escape density, because every escape is a scalar step in both implementations while the
//     runs between them are vector work here and a per-character loop there.
//
// NO RESTORE IS NEEDED ON THE NON-IN-PLACE ROWS: the destination is a separate buffer that is never
// read back, and the source is never modified -- the correctness harness asserts that last part. The
// IN-PLACE rows are the exception and they are the reason for the rotation and for the diagnostic
// below: an in-place unescape consumes its own input, so it has to be restored, and a restore is a
// memcpy of the whole string. That is heavy relative to the function, so it is (a) charged to BOTH
// sides equally, (b) placed on a buffer eight slots away from the one being processed, so it cannot
// stall the next call's wide load the way the restores that parked changes 142, 228, 230 and 241 did,
// and (c) printed on its own line so it can be subtracted. A restore heavier than the function
// REPLACES the measurement -- change 238's benchmark had to be rebuilt over exactly that.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

#define F_INPLACE     0x00100000u
#define F_EXTRA_INFO  0x02000000u

extern long wia_urlunescapew(wchar_t*, wchar_t*, unsigned long*, unsigned long);
extern void wia_uue_set_fallback(void*);
typedef HRESULT (WINAPI *FN)(const wchar_t*, wchar_t*, DWORD*, DWORD);
static FN sys;

#define ROT 8

/* the non-in-place shape: one source, a rotating destination, nothing to undo */
typedef struct { const wchar_t* s; DWORD cap; DWORD flags; wchar_t* d[ROT]; unsigned idx; } CASE;
/* the in-place shape: rotating buffers, each restored ROT slots after it was consumed */
typedef struct { const wchar_t* s; size_t n; DWORD flags; wchar_t* b[ROT]; unsigned idx; } IPCASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    DWORD cch = k->cap;
    return (uint64_t)(uint32_t)wia_urlunescapew((wchar_t*)k->s, k->d[i], &cch, k->flags);
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
    memcpy(k->b[old], k->s, (k->n + 1) * sizeof(wchar_t));   /* restore, ROT slots away */
    DWORD cch = 0;
    return (uint64_t)(uint32_t)wia_urlunescapew(k->b[i], 0, &cch, k->flags | F_INPLACE);
}
static uint64_t op_ip_sys(void* c){
    IPCASE* k = (IPCASE*)c;
    unsigned i = k->idx, old = (i + 1) & (ROT - 1);
    k->idx = old;
    memcpy(k->b[old], k->s, (k->n + 1) * sizeof(wchar_t));
    DWORD cch = 0;
    return (uint64_t)(uint32_t)sys(k->b[i], 0, &cch, k->flags | F_INPLACE);
}
/* the restore alone, for the diagnostic line */
static uint64_t op_ip_restore(void* c){
    IPCASE* k = (IPCASE*)c;
    unsigned i = k->idx, old = (i + 1) & (ROT - 1);
    k->idx = old;
    memcpy(k->b[old], k->s, (k->n + 1) * sizeof(wchar_t));
    return (uint64_t)(size_t)k->b[i];
}
#pragma optimize("", on)

/* PAGE-ALIGNED ARENAS, AND THIS IS NOT COSMETIC. The first version of this benchmark cut its
   subjects and destinations out of two large .bss arrays at whatever offsets the loop happened to
   produce, and it was NOT REPRODUCIBLE: the escape-dense row read 2371, 2378 and then 289 ns for
   the same call, and merely adding a diagnostic block ahead of the table -- which moved nothing but
   the layout -- changed five other rows by up to 35%. The 289 was the truth: a standalone harness
   that VirtualAllocs page-aligned arenas measured 296-305 ns for that row at EVERY one of sixteen
   destination page offsets, and 297 ns with one, two, four or eight rotating destinations.
   So the instability was the .bss placement, and the fix is to stop leaving it to chance: every
   subject and every destination starts on a page boundary, so each row's source/destination
   relationship is fixed, documented and identical from run to run. Same lesson as changes 142, 228,
   230 and 241 -- where a buffer's ADDRESS, not its contents, decided the verdict -- and as the URL
   survey, where a 16 KB stack local moved an unrelated row by 2x. */
static wchar_t* spool;
static wchar_t* dpool;
static unsigned long scur, dcur;
#define PAGEW 2048                    /* characters per page-aligned slot */

/* build a subject: n characters, one escape every `every` characters (0 = none).
   Each subject starts on a page boundary. */
static const wchar_t* mk(int n, int every)
{
    wchar_t* p = spool + scur;
    int k = 0;
    while (k < n) {
        if (every && (k % every) == 0 && k + 3 <= n) {
            p[k] = L'%'; p[k+1] = L'4'; p[k+2] = L'1'; k += 3;
        } else {
            p[k] = (wchar_t)(L'a' + k % 26); ++k;
        }
    }
    p[n] = 0;
    scur += PAGEW;                    /* the next subject starts on the next page */
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "UrlUnescapeW");
    if (!sys) { printf("cannot resolve UrlUnescapeW\n"); return 1; }
    wia_uue_set_fallback((void*)sys);
    /* page-aligned, so no row inherits an accidental source-to-destination offset */
    spool = (wchar_t*)VirtualAlloc(0, 1 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    dpool = (wchar_t*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!spool || !dpool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 11 };
    static const int LEN[N]   = { 16, 63, 64, 100, 256, 1000, 1000, 1000, 1000, 16, 1000 };
    static const int EVERY[N] = {  0,  0,  0,   0,   0,    0,   12,    3,    0,  0,    0 };
    static const int IP[N]    = {  0,  0,  0,   0,   0,    0,    0,    0,    0,  1,    1 };
    static const DWORD FLG[N] = {  0,  0,  0,   0,   0,    0,    0,    0, F_EXTRA_INFO, 0, 0 };
    static const char* names[] = {
        "16 plain", "63 plain (inline)", "64 plain (LocalAlloc)", "100 plain", "256 plain",
        "1000 plain", "1000, 1 esc/12", "1000, all escapes", "1000 plain +extrainfo",
        "16 in place", "1000 in place" };
    static CASE C[N]; static IPCASE IPC[N]; static wia_case cs[N];

    for (int i = 0; i < N; ++i) {
        const wchar_t* s = mk(LEN[i], EVERY[i]);
        size_t n = wcslen(s);
        if (IP[i]) {
            IPC[i].s = s; IPC[i].n = n; IPC[i].flags = FLG[i]; IPC[i].idx = 0;
            for (int q = 0; q < ROT; ++q) {
                IPC[i].b[q] = dpool + dcur; dcur += PAGEW;
                memcpy(IPC[i].b[q], s, (n + 1) * sizeof(wchar_t));
            }
            cs[i].ours = op_ip_ours; cs[i].system = op_ip_sys; cs[i].ctx = &IPC[i];
        } else {
            C[i].s = s; C[i].cap = (DWORD)n + 1; C[i].flags = FLG[i]; C[i].idx = 0;
            for (int q = 0; q < ROT; ++q) {
                C[i].d[q] = dpool + dcur; dcur += PAGEW;
            }
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
        /* the arenas are 1 MB and 4 MB; eleven rows take 11 slots of the first and 88 of the second */
        if (dcur > 2000000 || scur > 500000) {
            printf("BENCH SETUP ERROR: pool too small (dcur=%lu scur=%lu)\n", dcur, scur);
            return 1;
        }
        cs[i].label = names[i];
        cs[i].bytes = n * sizeof(wchar_t);
    }

    /* PER-ROW DIAGNOSTIC. Every row states what it actually asked for and what came back, because a
       benchmark row whose shape is not what its label says is the single most expensive mistake this
       project makes: a subject whose escapable characters sat in the wrong URL segment made
       discovery's UrlEscape row measure a no-op, and a 16 KB stack local moved another survey's
       numbers by 2x. One call per row, printed, is cheap insurance. */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each:\n");
        for (int i = 0; i < N; ++i) {
            size_t n = IP[i] ? IPC[i].n : wcslen(C[i].s);
            DWORD cch = IP[i] ? 0 : C[i].cap;
            long hr;
            if (IP[i]) {
                memcpy(IPC[i].b[0], IPC[i].s, (n + 1) * sizeof(wchar_t));
                hr = wia_urlunescapew(IPC[i].b[0], 0, &cch, IPC[i].flags | F_INPLACE);
                memcpy(IPC[i].b[0], IPC[i].s, (n + 1) * sizeof(wchar_t));
            } else {
                hr = wia_urlunescapew((wchar_t*)C[i].s, C[i].d[0], &cch, C[i].flags);
            }
            double t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
            printf("  %-22s inLen=%4llu cap=%5lu -> hr=%08lX cch=%5lu   ours(60) %9.2f ns\n",
                   names[i], (unsigned long long)n, (unsigned long)(IP[i] ? 0 : C[i].cap),
                   (unsigned long)hr, (unsigned long)cch, t);
        }
        printf("\n");
    }

    /* WHY THE SAME ROW MEASURES TWO DIFFERENT NUMBERS. The per-row diagnostic above reported the
       escape-dense row at 291 ns while the table below reported 2378 for the same call, and a
       standalone harness agreed with the 291. The difference is not the function and not the
       rotation: it is what ran immediately before. This block isolates it. */
    {
        volatile uint64_t sink = 0;
        int dense = 7;
        double a = wia_measure(cs[dense].ours, cs[dense].ctx, 60, &sink);
        double b = wia_measure(cs[dense].ours, cs[dense].ctx, 300, &sink);
        double c = wia_measure(cs[dense].system, cs[dense].ctx, 60, &sink);
        double d = wia_measure(cs[dense].ours, cs[dense].ctx, 60, &sink);
        double e = wia_measure(cs[dense].ours, cs[dense].ctx, 300, &sink);
        printf("the escape-dense row, measured five ways in a row:\n");
        printf("  ours, 60 trials, cold                 %9.2f ns\n", a);
        printf("  ours, 300 trials                      %9.2f ns\n", b);
        printf("  the live export, 60 trials            %9.2f ns\n", c);
        printf("  ours, 60 trials, AFTER the live one   %9.2f ns\n", d);
        printf("  ours, 300 trials, AFTER the live one  %9.2f ns\n", e);
        printf("\n");
    }

    /* the in-place restore's own cost, so the two in-place rows can be read honestly */
    {
        volatile uint64_t sink = 0;
        printf("the in-place rows carry a memcpy restore; its own cost, measured on this run:\n");
        for (int i = 0; i < N; ++i) {
            if (!IP[i]) continue;
            double r = wia_measure(op_ip_restore, &IPC[i], 60, &sink);
            printf("  %-22s restore alone %8.2f ns  (subtract it from both sides of that row)\n",
                   names[i], r);
        }
        printf("\n");
    }

    return wia_bench_compare("shlwapi UrlUnescapeW (wia two AVX2 passes vs five scalar walks plus a "
                             "LocalAlloc; GB/s counts INPUT bytes)", cs, N, 300);
}
