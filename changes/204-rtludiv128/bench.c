// changes/204-rtludiv128/bench.c
// Gate 2: time wia_udiv128 against the live ntdll!RtlUdiv128.
//
// The shipped function costs the same for every input -- it always runs 64 iterations -- so the
// classes here are chosen to separate OUR two paths rather than to vary a size: the hardware-divide
// region, the saturating region, and a divisor of 0. A random mix is included last so a
// branch-predictor-friendly single-path benchmark cannot flatter the result.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern unsigned __int64 wia_udiv128(unsigned __int64, unsigned __int64,
                                    unsigned __int64, unsigned __int64*);
typedef unsigned __int64 (NTAPI *FN)(unsigned __int64, unsigned __int64,
                                     unsigned __int64, unsigned __int64*);
static FN sys;

typedef struct { unsigned __int64 hi, lo, d; int mix; } CASE;
static unsigned __int64 rem_out;

/* A fixed pseudo-random table for the mixed class, so both sides see the same inputs. */
#define MIXN 256
static unsigned __int64 MH[MIXN], ML[MIXN], MD[MIXN];

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    if (!k->mix) return (uint64_t)wia_udiv128(k->hi, k->lo, k->d, &rem_out);
    uint64_t acc = 0;
    for (int i = 0; i < MIXN; ++i) acc ^= wia_udiv128(MH[i], ML[i], MD[i], &rem_out);
    return acc;
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    if (!k->mix) return (uint64_t)sys(k->hi, k->lo, k->d, &rem_out);
    uint64_t acc = 0;
    for (int i = 0; i < MIXN; ++i) acc ^= sys(MH[i], ML[i], MD[i], &rem_out);
    return acc;
}
#pragma optimize("", on)

int main(void){
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    sys = (FN)GetProcAddress(h, "RtlUdiv128");

    {   /* half in the divide region, half saturating, so the mix really is mixed */
        unsigned __int64 s = 0x2040204ull;
        for (int i = 0; i < MIXN; ++i) {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            unsigned __int64 a = s >> 16;
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            MD[i] = ((a << 32) ^ (s >> 16)) | 1;
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            ML[i] = s >> 8;
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            MH[i] = (i & 1) ? (MD[i] + (s >> 40)) : ((s >> 40) % (MD[i] ? MD[i] : 1));
        }
    }

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "hi<d (one div)", "hi=0, 64/64", "hi=d-1 (edge, safe)",
        "hi>d (saturates)", "divisor 0", "random mix x256" };
    C[0].hi=0x1234ull;              C[0].lo=0xDEADBEEFCAFEBABEull; C[0].d=0x9E3779B97F4A7C15ull;
    C[1].hi=0;                      C[1].lo=0xFFFFFFFFFFFFFFFFull; C[1].d=0x1234567ull;
    C[2].hi=0x9E3779B97F4A7C14ull;  C[2].lo=0xFFFFFFFFFFFFFFFFull; C[2].d=0x9E3779B97F4A7C15ull;
    C[3].hi=0xFFFFFFFFFFFFFFFFull;  C[3].lo=0xFFFFFFFFFFFFFFFFull; C[3].d=2;
    C[4].hi=0x1234ull;              C[4].lo=1000;                   C[4].d=0;
    C[5].mix = 1;

    /* "bytes" is only used for the GB/s column; one 128-bit dividend = 16 bytes. */
    static const size_t bytes[] = { 16, 16, 16, 16, 16, 16 * MIXN };
    for (int i = 0; i < N; ++i) {
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("ntdll RtlUdiv128 (wia one hardware div vs a 64-iteration software loop)",
                             cs, N, 300);
}
