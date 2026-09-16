// changes/244-hashdata/bench.c
// Gate 2: time wia_hashdata against the live shlwapi!HashData.
//
// THE CASE MIX IS THE WHOLE DESIGN HERE, because this function has TWO size parameters and its cost
// is their product. probes/cost.c measured the shipped surface across cbData x cbHash and found the
// per-lookup cost FLAT at 0.42 ns for every digest of six bytes or more, and RISING as the digest
// shrinks -- 0.51 ns at four bytes, 0.74 at two, 1.06 at one -- because a one-lane digest has no
// parallelism left and the shipped loop's memory chain becomes the limit.
//
// That shape is exactly where a twelve-lanes-in-registers implementation is weakest. A pass over the
// source costs the same whether it advances two lanes or twelve, so at cbHash = 1 we do twelve times
// the arithmetic for one byte of result and win only what the shipped loop wastes on memory traffic.
// The small digests are therefore in the table on purpose, at the shortest source length as well as
// the longest -- they are the rows that decide whether this lands.
//
// NO RESTORE IS NEEDED. The digest is written to a buffer that is not read back, and the source is
// never modified, so unlike the in-place path functions in this repository there is nothing to undo
// between calls -- and therefore none of the restore artefacts that parked changes 142, 228, 230 and
// 241. The destination still ROTATES across four buffers, for the same reason those changes now do:
// it costs nothing and removes the question entirely.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern long wia_hashdata(const unsigned char*, unsigned long, unsigned char*, unsigned long);
typedef HRESULT (WINAPI *FN)(const BYTE*, DWORD, BYTE*, DWORD);
static FN sys;

#define ROT 4
typedef struct { const unsigned char* s; unsigned long n; unsigned long m;
                 unsigned char* d[ROT]; unsigned idx; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx;
    k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)wia_hashdata(k->s, k->n, k->d[i], k->m);
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx;
    k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)sys(k->s, (DWORD)k->n, k->d[i], (DWORD)k->m);
}
#pragma optimize("", on)

static unsigned char src[8192];
static unsigned char dpool[16384];

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "HashData");
    if (!sys) { printf("cannot resolve HashData\n"); return 1; }
    for (int i = 0; i < 8192; ++i) src[i] = (unsigned char)(i * 31 + 7);

    enum { N = 14 };
    /* (cbData, cbHash). The first three rows are the ones a twelve-lane implementation cannot win
       easily; the last rows are the shape the function is actually called in. */
    static const unsigned long DN[N] = { 16,  16,  16,  16,   64,  64,  256, 256, 256,
                                         4096, 4096, 4096, 4096, 4096 };
    static const unsigned long HN[N] = {  1,   2,   4,  16,    1,  16,    1,   4,  16,
                                            1,    4,   16,   32,  128 };
    static const char* names[] = {
        "16B -> 1", "16B -> 2", "16B -> 4", "16B -> 16",
        "64B -> 1", "64B -> 16",
        "256B -> 1", "256B -> 4", "256B -> 16",
        "4K -> 1", "4K -> 4", "4K -> 16", "4K -> 32", "4K -> 128" };
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];

    unsigned long dcur = 0;
    for (int i = 0; i < N; ++i) {
        C[i].s = src; C[i].n = DN[i]; C[i].m = HN[i]; C[i].idx = 0;
        for (int q = 0; q < ROT; ++q) {
            C[i].d[q] = dpool + dcur;
            dcur += HN[i] + 64;                 /* room plus a gap, so no two cases share a line */
            if (dcur > sizeof dpool) { printf("BENCH SETUP ERROR: pool too small\n"); return 1; }
        }
        /* the work is one table lookup per (source byte x digest byte); report the SOURCE bytes as
           the throughput denominator, which is what a caller hashing a buffer cares about */
        bytes[i] = (size_t)DN[i];
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }

    return wia_bench_compare("shlwapi HashData (wia twelve lanes in registers vs one lane at a time "
                             "through memory; GB/s counts SOURCE bytes, so a wider digest reads as "
                             "slower on both sides)", cs, N, 300);
}
