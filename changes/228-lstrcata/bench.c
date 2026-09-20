// changes/228-lstrcata/bench.c
// Gate 2: time wia_lstrcata against the live kernelbase!lstrcatA.
// The destination is re-terminated every iteration by both sides, so neither accumulates.
// Lengths are COMPUTED, never hardcoded.
//
// The case mix is the point. lstrcat is a length scan of the destination plus a copy of the source,
// and those two halves scale with different inputs. Appending 64 bytes to a 4000-byte buffer costs
// almost as much as copying the whole buffer, the accidental quadratic a caller hits appending in
// a loop, so the "onto 4000" rows matter more than the raw throughput rows.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern char* wia_lstrcata(char*, const char*);
typedef char* (WINAPI *FN)(char*, const char*);
static FN sys;

/* The restore's address, not its cost. The restore is already minimal, one store putting back the
   destination's terminator -- but it landed on the buffer the next call was about to read, and for the
   short-onto-short rows that store sits INSIDE the first 32-byte block the destination scan loads. A
   wide load overlapping a just-retired narrow store cannot use store-to-load forwarding: it waits for
   the store to drain, while the shipped byte loop reads narrowly and forwards from it cheaply. Change
   241 measured this shape directly -- ours moved 2.9x, the live export 3% -- and it is what parked that
   change for five runs, its wide sibling 230, and this one.

   So the destination rotates: the store lands on the buffer the PREVIOUS call dirtied. Same single
   store, same single call, one address apart. The diagnostic in main() reprints both shapes. */
#define ROT 4
typedef struct { char* d; int dn; const char* s; char* ds[ROT]; unsigned idx; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k=(CASE*)c;
    unsigned i=k->idx, prev=(i+ROT-1)&(ROT-1);
    k->ds[prev][k->dn]=0;
    k->idx=(i+1)&(ROT-1);
    return (uint64_t)(size_t)wia_lstrcata(k->ds[i], k->s);
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
                                       return (uint64_t)(size_t)wia_lstrcata(k->d, k->s); }
static uint64_t op_sys_same (void* c){ CASE* k=(CASE*)c; k->d[k->dn]=0;
                                       return (uint64_t)(size_t)sys(k->d, k->s); }
#pragma optimize("", on)

static char dpool[80000];
/* 16384, and the offsets below are spaced so no case's source lands inside another's. They
   were not, at first: a 4000-byte source at offset 100 was truncated to 164 bytes by the next
   case writing its terminator at offset 264, and the "4000 onto empty" row then reported 461
   GB/s -- faster than memcpy, which is how the bug announced itself. */
static char spool[16384];

static char* mkd(int off, int n){
    char* p = dpool + off;
    for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
    p[n] = 0;
    return p;
}
static const char* mks(int off, int n){
    char* p = spool + off;
    for (int i = 0; i < n; ++i) p[i] = (char)('A' + i % 26);
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcatA");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcatA") : 0; }

    enum { N = 9 };
    /* The first three rows are deliberately the SHORT-onto-SHORT shapes, which is where a
       vectorised implementation cannot win: the whole job is smaller than the setup. They are
       here to show the crossover honestly rather than to be avoided. */
    static const int DN[N] = {    0,    8,   16,    0,    0,   64,  1024,  4000,  4000 };
    static const int SN[N] = {    8,    8,   16,   64, 4000,   64,    64,    64,  4000 };
    static const char* names[] = {"8 onto empty","8 onto 8","16 onto 16",
                                  "64 onto empty","4000 onto empty",
                                  "64 onto 64","64 onto 1024","64 onto 4000","4000 onto 4000"};
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];

    /* Offsets are computed, not hardcoded. Hand-picked offsets let one case's buffer land inside
       another's twice while this file was being written: a 4000-byte source at a low offset was
       truncated by the next case writing its terminator, and the "4000 onto empty" row reported
       461 GB/s -- faster than memcpy, which is how it announced itself. Each destination is given
       room for its own length PLUS the append PLUS a 64-byte gap, and each source likewise. */
    {
        int dcur = 0, scur = 0;
        for (int i = 0; i < N; ++i) {
            C[i].d  = mkd(dcur, DN[i]);
            C[i].dn = DN[i];
            C[i].s  = mks(scur, SN[i]);
            dcur += DN[i] + SN[i] + 64;
            /* the rotating destinations: identical content, so every iteration times the same call */
            C[i].ds[0] = C[i].d;
            C[i].idx = 0;
            for (int q = 1; q < ROT; ++q) {
                C[i].ds[q] = mkd(dcur, DN[i]);
                dcur += DN[i] + SN[i] + 64;
            }
            scur += SN[i] + 64;
            if (dcur > (int)sizeof dpool || scur > (int)sizeof spool) {
                printf("BENCH SETUP ERROR: pool too small (dcur=%d, scur=%d)\n", dcur, scur);
                return 1;
            }
            /* the work is the destination scan PLUS the source copy */
            bytes[i] = (size_t)(DN[i] + SN[i]);
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

    return wia_bench_compare("kernelbase lstrcatA (wia AVX2 scan + page-clamped append vs kernelbase "
                             "byte loop; the restore is ONE store, on a ROTATED destination)",
                             cs, N, 300);
}
