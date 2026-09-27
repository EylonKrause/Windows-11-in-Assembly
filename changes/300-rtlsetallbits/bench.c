// changes/300-rtlsetallbits/bench.c
// Gate 2: time wia_setallbits against the live ntdll!RtlSetAllBits across bitmap sizes.
//
// The size classes are chosen around where discovery/bitmap_setall.c found the gap. Below 8 KB the
// live export costs 2.7x-3.9x what RtlClearAllBits costs for identical work, and that is the whole
// opportunity; from 8 KB up the two already agree at ~130 bytes/ns and the only thing that matters
// is not regressing. So the small rows carry the win and the large rows carry the risk, and both
// have to be in the table for the verdict to mean anything.
//
// The buffer is allocated once, at a fixed address, and every case fills a prefix of it, so no row
// is measuring an allocation or a first-touch page fault.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RTLBM;
typedef VOID (NTAPI *PFN)(RTLBM*);

extern void wia_setallbits(RTLBM*);
static PFN sys;

#pragma optimize("", off)
static uint64_t op_ours(void* c) { wia_setallbits((RTLBM*)c); return ((RTLBM*)c)->Buffer[0]; }
static uint64_t op_sys (void* c) { sys((RTLBM*)c);            return ((RTLBM*)c)->Buffer[0]; }
#pragma optimize("", on)

int main(void) {
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    sys = (PFN)GetProcAddress(nt, "RtlSetAllBits");
    if (!sys) { printf("no RtlSetAllBits\n"); return 2; }

    enum { K = 9 };
    static const ULONG BITS[K] = { 32, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576 };
    static const char* NAME[K] = { "32 bits", "64 bits", "256 bits", "1 Kibit", "4 Kibit",
                                   "16 Kibit", "64 Kibit", "256 Kibit", "1 Mibit" };

    SIZE_T cap = (1048576u / 8u) + 4096u;
    PULONG buf = (PULONG)VirtualAlloc(NULL, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memset(buf, 0, cap);                      // first-touch every page before timing

    static RTLBM bm[K];
    static wia_case cs[K];
    for (int i = 0; i < K; ++i) {
        bm[i].SizeOfBitMap = BITS[i];
        bm[i].Buffer = buf;
        cs[i].label  = NAME[i];
        cs[i].bytes  = (size_t)(((BITS[i] + 31u) / 32u) * 4u);
        cs[i].ours   = op_ours;
        cs[i].system = op_sys;
        cs[i].ctx    = &bm[i];
    }
    return wia_bench_compare("ntdll RtlSetAllBits  (wia AVX2 fill vs ntdll)", cs, K, 300);
}
