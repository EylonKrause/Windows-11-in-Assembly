// changes/247-pathaddextensionw/bench.c
// Gate 2: time wia_pathaddextensionw against the live shlwapi!PathAddExtensionW.
//
// The first three rows are the point. a disassembly fan-out built a C prototype of this function
// before it was written and measured 1.34x to 5.74x on change 132's size classes -- but also a
// Reproducible regression at an empty path (0.87-0.94x) and marginal rows at two to six characters,
// because the shipped function answers a short path from its first characters while a vectorised one
// still pays a full setup. That is exactly the shape change 244 had to solve, and the only way to know
// whether it bites here is to put those rows in the table rather than start at sixteen characters.
//
// The restore is one store, on a rotated buffer. This function appends in place, so the buffer must be
// undone between calls -- but it only ever writes from the append point onward, so putting the
// terminator back at that point is the whole restore. One store, and it lands on the buffer the
// PREVIOUS call dirtied rather than the one the next call is about to read: a wide load overlapping a
// just-retired narrow store cannot use store-to-load forwarding, and that single fact is what parked
// changes 142, 228, 230 and 241 for five runs each. The refusal rows write nothing at all and get no
// restore, which the per-row diagnostic asserts.
//
// The arenas are page-aligned for the reason change 245 had to learn the hard way: its first benchmark
// cut its buffers out of .bss at whatever offsets the setup loop produced and was not reproducible,
// reading 2371, 2378 and then 289 ns for the same call.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_pathaddextensionw(wchar_t*, const wchar_t*);
typedef BOOL (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

#define ROT 4
#define PAGEW 2048
typedef struct {
    const wchar_t* ext;
    unsigned long n;            /* the append point: where the terminator goes back */
    int writes;                 /* does this row write at all? */
    wchar_t* b[ROT];
    unsigned idx;
} CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx, prev = (i + ROT - 1) & (ROT - 1);
    if (k->writes) k->b[prev][k->n] = 0;          /* one store, on the PREVIOUS buffer */
    k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)wia_pathaddextensionw(k->b[i], k->ext);
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx, prev = (i + ROT - 1) & (ROT - 1);
    if (k->writes) k->b[prev][k->n] = 0;
    k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)sys(k->b[i], k->ext);
}
#pragma optimize("", on)

static wchar_t* pool;
static unsigned long pcur;

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathAddExtensionW");
    if (!sys) { printf("cannot resolve PathAddExtensionW\n"); return 1; }
    pool = (wchar_t*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!pool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 10 };
    /* (path length, has-an-extension, extension) */
    static const int LEN[N]  = { 0, 2, 6, 16, 64, 128, 254, 300, 40, 16 };
    static const int HASX[N] = { 0, 0, 0,  0,  0,   0,   0,   0,  1,  0 };
    static const wchar_t* EXT[N] = { L".exe", L".exe", L".exe", L".exe", L".exe", L".exe",
                                     L".exe", L".exe", L".exe", 0 };
    static const char* names[N] = {
        "empty path", "2 chars", "6 chars", "16 chars", "64 chars", "128 chars", "254 chars",
        "300 chars (refused)", "already has one", "16 chars, NULL ext" };
    static CASE C[N]; static wia_case cs[N];

    for (int i = 0; i < N; ++i) {
        int n = LEN[i];
        C[i].ext = EXT[i];
        C[i].n = (unsigned long)n;
        C[i].idx = 0;
        for (int q = 0; q < ROT; ++q) {
            wchar_t* p = pool + pcur;
            pcur += PAGEW;
            for (int k = 0; k < n; ++k) p[k] = (k % 9 == 8) ? L'\\' : (wchar_t)(L'a' + k % 23);
            if (n > 2) { p[0] = L'C'; p[1] = L':'; p[2] = L'\\'; }
            if (HASX[i] && n > 5) { p[n - 4] = L'.'; p[n - 3] = L'x'; p[n - 2] = L'y';
                                    p[n - 1] = L'z'; }
            p[n] = 0;
            C[i].b[q] = p;
        }
        /* does it write? Ask the function itself on a scratch copy rather than assume. */
        {
            wchar_t* scratch = pool + pcur;
            pcur += PAGEW;
            memcpy(scratch, C[i].b[0], (size_t)(n + 1) * sizeof(wchar_t));
            C[i].writes = wia_pathaddextensionw(scratch, EXT[i]) ? 1 : 0;
        }
        if (pcur > (4u << 19)) { printf("BENCH SETUP ERROR: pool too small\n"); return 1; }
        cs[i].label = names[i];
        cs[i].bytes = (size_t)n * sizeof(wchar_t);
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }

    /* per-row shape and the restore assertion, so a row whose label does not match what it does
       cannot hide -- and so the restore is never charged to a row that writes nothing */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each (and whether the row needs a restore at all):\n");
        for (int i = 0; i < N; ++i) {
            static wchar_t scratch[600];
            int r;
            memcpy(scratch, C[i].b[0], (size_t)(LEN[i] + 1) * sizeof(wchar_t));
            r = wia_pathaddextensionw(scratch, C[i].ext);
            double t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
            printf("  %-22s len=%3d -> BOOL=%d  %-14s result \"%.24ls\"%s  ours(60) %7.2f ns\n",
                   names[i], LEN[i], r, C[i].writes ? "writes: restore" : "writes NOTHING",
                   scratch, LEN[i] > 24 ? "..." : "", t);
        }
        printf("\n");
    }

    return wia_bench_compare("shlwapi PathAddExtensionW (wia AVX2 over change 132's extension point "
                             "vs the shipped envelope; the restore is ONE store on a ROTATED buffer)",
                             cs, N, 300);
}
