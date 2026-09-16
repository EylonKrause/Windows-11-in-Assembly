// changes/230-lstrcatw/bench.c
// Gate 2: time wia_lstrcatw against the live kernelbase!lstrcatW.
// Pool offsets are COMPUTED with guaranteed spacing -- hand-placed ones let case buffers overlap
// twice while change 228 was being written, and the only thing that caught it was a row reporting
// a throughput above memcpy's.
//
// THE CASE MIX. lstrcat is a destination scan plus a source copy, and those scale with different
// inputs. The short-onto-short rows are here because the NARROW sibling (change 228) had to be
// PARKED for losing there, and the same question has to be asked of this one honestly rather than
// avoided. "64 onto 4000" is the shape the function is usually used in and the survey's worst
// number: 1643 ns.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern wchar_t* wia_lstrcatw(wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

/* THE RESTORE'S ADDRESS, not its cost. This restore is already minimal -- one store putting back the
   destination's terminator -- but it landed on the buffer the next call was about to read, and for the
   "8 onto 8" row that store sits INSIDE the first 32-byte block the destination scan loads. A wide load
   overlapping a just-retired narrow store cannot use store-to-load forwarding: it waits for the store to
   drain, while the shipped SSE2 loop reads narrowly and forwards from it cheaply. Change 241 measured
   the same shape directly -- ours moved 2.9x, the live export 3% -- which is what parked that change for
   five runs and, measured here, is what parked this one.

   So the destination rotates: the store lands on the buffer the PREVIOUS call dirtied. Same single
   store, same single call, one address apart. The diagnostic in main() reprints both shapes every run. */
#define ROT 4
typedef struct { wchar_t* d; int dn; const wchar_t* s; wchar_t* ds[ROT]; unsigned idx; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k=(CASE*)c;
    unsigned i=k->idx, prev=(i+ROT-1)&(ROT-1);
    k->ds[prev][k->dn]=0;
    k->idx=(i+1)&(ROT-1);
    return (uint64_t)(size_t)wia_lstrcatw(k->ds[i], k->s);
}
static uint64_t op_sys (void* c){
    CASE* k=(CASE*)c;
    unsigned i=k->idx, prev=(i+ROT-1)&(ROT-1);
    k->ds[prev][k->dn]=0;
    k->idx=(i+1)&(ROT-1);
    return (uint64_t)(size_t)sys(k->ds[i], k->s);
}
/* the OLD shape, kept only to reprint the artefact: restore and call on the SAME buffer */
static uint64_t op_ours_same(void* c){ CASE* k=(CASE*)c; k->d[k->dn]=0;
                                       return (uint64_t)(size_t)wia_lstrcatw(k->d, k->s); }
static uint64_t op_sys_same (void* c){ CASE* k=(CASE*)c; k->d[k->dn]=0;
                                       return (uint64_t)(size_t)sys(k->d, k->s); }
#pragma optimize("", on)

static wchar_t dpool[80000];
static wchar_t spool[16384];

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcatW");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcatW") : 0; }

    enum { N = 9 };
    static const int DN[N] = {    0,    8,   16,    0,    0,   64,  1024,  4000,  4000 };
    static const int SN[N] = {    8,    8,   16,   64, 4000,   64,    64,    64,  4000 };
    static const char* names[] = {"8 onto empty","8 onto 8","16 onto 16",
                                  "64 onto empty","4000 onto empty",
                                  "64 onto 64","64 onto 1024","64 onto 4000","4000 onto 4000"};
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int dcur = 0, scur = 0;
        for (int i = 0; i < N; ++i) {
            wchar_t* dp = dpool + dcur;
            for (int k = 0; k < DN[i]; ++k) dp[k] = (wchar_t)(L'a' + k % 23);
            dp[DN[i]] = 0;
            wchar_t* sp = spool + scur;
            for (int k = 0; k < SN[i]; ++k) sp[k] = (wchar_t)(L'A' + k % 26);
            sp[SN[i]] = 0;
            C[i].d = dp; C[i].dn = DN[i]; C[i].s = sp;
            dcur += DN[i] + SN[i] + 64;
            /* the rotating destinations: identical content, so every iteration times the same call */
            C[i].ds[0] = dp;
            C[i].idx = 0;
            for (int q = 1; q < ROT; ++q) {
                wchar_t* extra = dpool + dcur;
                dcur += DN[i] + SN[i] + 64;
                if (dcur > 79000) { printf("BENCH SETUP ERROR: dpool overflow\n"); return 1; }
                memcpy(extra, dp, (size_t)(DN[i] + 1) * sizeof(wchar_t));
                C[i].ds[q] = extra;
            }
            scur += SN[i] + 64;
            if (dcur > 79000 || scur > 16000) { printf("BENCH SETUP ERROR\n"); return 1; }
            bytes[i] = (size_t)(DN[i] + SN[i]) * sizeof(wchar_t);
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }
    /* reprint the artefact, measured on this run: the same single store, one address apart */
    {
        volatile uint64_t sink = 0;
        printf("the restore's ADDRESS, measured on this run (same work, one address apart):\n");
        for (int i = 0; i < N; ++i) {
            if (DN[i] > 64) continue;                 /* the hazard needs the store near the scan start */
            double so = wia_measure(op_ours_same, &C[i], 60, &sink);
            double sl = wia_measure(op_sys_same,  &C[i], 60, &sink);
            double ro = wia_measure(op_ours,      &C[i], 60, &sink);
            double rl = wia_measure(op_sys,       &C[i], 60, &sink);
            printf("  %-16s same buffer: ours %6.2f live %6.2f -> %5.2fx   rotated: ours %6.2f "
                   "live %6.2f -> %5.2fx\n",
                   names[i], so, sl, so > 0 ? sl/so : 0.0, ro, rl, ro > 0 ? rl/ro : 0.0);
        }
        printf("\n");
    }

    return wia_bench_compare("kernelbase lstrcatW (wia AVX2 clamped scan + append vs kernelbase SSE2; "
                             "the restore is ONE store, on a ROTATED destination)", cs, N, 300);
}
