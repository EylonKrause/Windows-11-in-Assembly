/* changes/142-pathaddbackslashw/bench.c
   Gate 2: time wia_pathaddbackslashw against the live shlwapi!PathAddBackslashW.

   THE RESTORE IS ONE STORE, ON A ROTATED BUFFER. This file used to restore with a memcpy of the WHOLE
   string into one shared buffer immediately before each call, which got both halves of the restore
   lesson wrong at once, and it is why this change was parked at 0.85x on the 16-character row:

     * THE COST (change 238's lesson): the function writes AT MOST TWO CHARACTERS -- a separator at
       p[n] and a terminator at p[n+1] -- so undoing it needs a single 2-byte store, putting back the
       terminator the append overwrote. A 34-byte memcpy against a function costing about two
       nanoseconds does not add noise, IT REPLACES THE MEASUREMENT.
     * THE ADDRESS (change 241's lesson): a store immediately in front of a call whose first act is a
       64-byte vector load covering that address cannot use store-to-load forwarding -- the load waits
       for the store to drain -- while the shipped implementation, which reads one character at a time,
       forwards from it cheaply. Measured on change 241's identically-shaped function, with the same
       work on both sides and only the ADDRESS different, ours moved 2.9x and the live export 3%.

   So each row now holds four buffers and restores the one the PREVIOUS call dirtied, and the setup
   ASSERTS for every row that the single store really does return the buffer to byte-identical -- and
   that the rows expected to write do write, and the row expected to decline does not. The diagnostic
   below reprints both shapes every run, so the artefact stays visible rather than being designed out.

   The 1024-character row writes NOTHING: the MAX_PATH rule refuses any path whose result would not fit
   in 260 characters, returning NULL, so that row is the honest floor and pays no restore at all. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

extern wchar_t* wia_pathaddbackslashw(wchar_t*);
typedef wchar_t* (WINAPI *fn)(wchar_t*);
static fn sys;

#define ROT 4
typedef struct {
    const wchar_t* src;
    int n;
    wchar_t* bufs[ROT];
    unsigned idx;
    int writes;
} CASE;

#pragma optimize("", off)
/* one store, on the buffer the PREVIOUS call dirtied */
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx, prev = (i + ROT - 1) & (ROT - 1);
    k->bufs[prev][k->n] = 0;
    k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uintptr_t)wia_pathaddbackslashw(k->bufs[i]);
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx, prev = (i + ROT - 1) & (ROT - 1);
    k->bufs[prev][k->n] = 0;
    k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uintptr_t)sys(k->bufs[i]);
}
/* the declining row writes nothing, so there is nothing to undo */
static uint64_t op_ours_n(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)(uintptr_t)wia_pathaddbackslashw(k->bufs[0]);
}
static uint64_t op_sys_n(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)(uintptr_t)sys(k->bufs[0]);
}
/* the OLD shape, kept only to reprint the artefact: whole-string memcpy into one shared buffer */
static wchar_t oldwork[1200];
static uint64_t op_ours_old(void* c){
    CASE* k = (CASE*)c;
    memcpy(oldwork, k->src, (size_t)(k->n + 1) * 2);
    return (uint64_t)(uintptr_t)wia_pathaddbackslashw(oldwork);
}
static uint64_t op_sys_old(void* c){
    CASE* k = (CASE*)c;
    memcpy(oldwork, k->src, (size_t)(k->n + 1) * 2);
    return (uint64_t)(uintptr_t)sys(oldwork);
}
#pragma optimize("", on)

static wchar_t s16[32], s64[96], s254[300], s1024[1100], sreal[96];
static wchar_t pool[24][1200];   /* 5 rows x ROT buffers, plus one probe */
static void fill(wchar_t* b, int n){ for (int i = 0; i < n; ++i) b[i] = L'a' + (i % 23); b[n] = 0; }

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (fn)GetProcAddress(h, "PathAddBackslashW");
    if (!sys) { printf("BENCH SETUP ERROR: cannot resolve PathAddBackslashW\n"); return 1; }
    fill(s16, 16); fill(s64, 64); fill(s254, 254); fill(s1024, 1024);
    { const wchar_t* r = L"C:\\Program Files\\Windows NT\\Accessories\\wordpad.exe";
      int i = 0; for (; r[i]; ++i) sreal[i] = r[i]; sreal[i] = 0; }

    enum { N = 5 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[N] = { "16", "64", "254", "1024 (declines)", "realpath" };
    const wchar_t* srcs[N] = { s16, s64, s254, s1024, sreal };
    /* COMPUTED, never hardcoded -- the previous version of this file carried 49 for a path that is 51
       characters long. It only skewed the reported GB/s there; here the length is the restore index, so
       the setup's own assertion caught it. */
    int L[N];
    for (int i = 0; i < N; ++i) L[i] = (int)wcslen(srcs[i]);

    printf("restore windows, asserted per row:\n");
    for (int i = 0; i < N; ++i) {
        static wchar_t snap[1200];
        wchar_t* probe = pool[23];
        C[i].src = srcs[i]; C[i].n = L[i]; C[i].idx = 0;
        for (int q = 0; q < ROT; ++q) {
            C[i].bufs[q] = pool[i * ROT + q];
            memcpy(C[i].bufs[q], srcs[i], (size_t)(L[i] + 1) * 2);
        }
        /* does this row write at all, and does the single store undo it exactly? Both sides. */
        memcpy(snap,  srcs[i], (size_t)(L[i] + 2) * 2);
        memcpy(probe, srcs[i], (size_t)(L[i] + 2) * 2);
        wia_pathaddbackslashw(probe);
        C[i].writes = memcmp(snap, probe, (size_t)(L[i] + 2) * 2) != 0;
        probe[L[i]] = 0;
        int bad1 = memcmp(snap, probe, (size_t)(L[i] + 1) * 2) != 0;
        memcpy(probe, srcs[i], (size_t)(L[i] + 2) * 2);
        sys(probe);
        int wrote2 = memcmp(snap, probe, (size_t)(L[i] + 2) * 2) != 0;
        probe[L[i]] = 0;
        int bad2 = memcmp(snap, probe, (size_t)(L[i] + 1) * 2) != 0;
        if (bad1 || bad2 || C[i].writes != wrote2) {
            printf("BENCH SETUP ERROR: row \"%s\" -- restore %s/%s, writes ours %d live %d\n",
                   names[i], bad1 ? "INEXACT" : "exact", bad2 ? "INEXACT" : "exact",
                   C[i].writes, wrote2);
            return 1;
        }
        printf("  %-16s %s\n", names[i],
               C[i].writes ? "writes: restores ONE character at [n]" : "writes NOTHING: no restore");
        cs[i].label = names[i]; cs[i].bytes = (size_t)L[i] * 2; cs[i].ctx = &C[i];
        cs[i].ours   = C[i].writes ? op_ours : op_ours_n;
        cs[i].system = C[i].writes ? op_sys  : op_sys_n;
    }

    /* reprint the artefact, measured on this run: the whole-string memcpy into one shared buffer,
       against one store on a rotated buffer -- same function, same call, same answer */
    {
        volatile uint64_t sink = 0;
        printf("\nthe restore's cost AND address, measured on this run:\n");
        for (int i = 0; i < N; ++i) {
            if (!C[i].writes) continue;
            double oo = wia_measure(op_ours_old, &C[i], 60, &sink);
            double ol = wia_measure(op_sys_old,  &C[i], 60, &sink);
            double no = wia_measure(op_ours,     &C[i], 60, &sink);
            double nl = wia_measure(op_sys,      &C[i], 60, &sink);
            printf("  %-16s memcpy+same buffer: ours %6.2f live %6.2f -> %5.2fx   one store+rotated: "
                   "ours %6.2f live %6.2f -> %5.2fx\n",
                   names[i], oo, ol, oo > 0 ? ol/oo : 0.0, no, nl, no > 0 ? nl/no : 0.0);
        }
        printf("\n");
    }

    return wia_bench_compare("shlwapi PathAddBackslashW (wia AVX2 vs shlwapi; the restore is ONE store, "
                             "on a ROTATED buffer so it cannot stall the next call's wide load)",
                             cs, N, 300);
}
