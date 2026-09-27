/* discovery/bitmap_setall.c
   RtlSetAllBits against RtlClearAllBits, which are the same function with a different constant.

   WHY. An earlier probe batch recorded RtlSetAllBits at 21 ns and RtlClearAllBits at 4.22 ns on
   the same bitmap. Those two do *identical* work -- fill SizeOfBitMap bits of Buffer with ones, or
   with zeroes -- so a 5x gap cannot be the workload. Either the Set path is missing an optimisation
   the Clear path has, or the earlier number was taken at a single size and is an artefact. The
   bitmap family is already live in this repository (256 RtlFindSetBits, 257 RtlNumberOfSetBits,
   260 RtlCopyBitMap), so the neighbours are converted and these two are not.

   WHAT THIS MEASURES, and why each column is here:

     * Both functions over four bitmap sizes spanning three orders of magnitude, 64 bits to 1 Mibit.
       A FIXED gap that does not grow with size is call-path overhead and is worth little. A gap
       that scales with size is a slower inner loop and is worth a change.
     * Bytes per nanosecond, the diagnostic this repository uses to read the loop width without
       disassembling: ~4-5 is a byte loop, ~22 is 16-byte SSE2, ~85 is 32-byte AVX2, and a memset
       that reaches ERMS or non-temporal stores runs far above that.
     * The ratio column directly. This is the whole question: if Set/Clear is ~1.0 at every size
       then the earlier 21-vs-4.22 reading did not survive contact with a size sweep and there is
       nothing here.
     * A tail size that is NOT a multiple of 64 bits, because the last partial word is where a
       fill-with-ones has to mask and a fill-with-zeroes does not, and that asymmetry is the one
       honest reason the two could legitimately differ.

   NOTHING HERE IS A CONTRACT. It is a shortlist.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RTLBM;

typedef VOID (NTAPI *PFN_INIT)(RTLBM*, PULONG, ULONG);
typedef VOID (NTAPI *PFN_ALL) (RTLBM*);

static PFN_INIT pInit;
static PFN_ALL  pSetAll, pClearAll;

static double freq_(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }
static volatile uint64_t sink;

static double timeit(PFN_ALL fn, RTLBM* bm) {
    double fq = freq_(); int inner = 64; double best = 1e300;
    for (int w = 0; w < 32; ++w) fn(bm);
    for (;;) {
        LARGE_INTEGER a,b; QueryPerformanceCounter(&a);
        for (int i = 0; i < inner; ++i) fn(bm);
        QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart-a.QuadPart)*1e9/fq;
        if (ns >= 300000.0 || inner >= (1<<24)) break;
        inner *= 4;
    }
    for (int t = 0; t < 200; ++t) {
        LARGE_INTEGER a,b; QueryPerformanceCounter(&a);
        for (int i = 0; i < inner; ++i) fn(bm);
        QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart-a.QuadPart)*1e9/fq/(double)inner;
        if (ns < best) best = ns;
    }
    sink ^= bm->Buffer[0];
    return best;
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    pInit     = (PFN_INIT)GetProcAddress(nt, "RtlInitializeBitMap");
    pSetAll   = (PFN_ALL) GetProcAddress(nt, "RtlSetAllBits");
    pClearAll = (PFN_ALL) GetProcAddress(nt, "RtlClearAllBits");
    if (!pInit || !pSetAll || !pClearAll) { printf("missing export\n"); return 2; }

    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    /* the last entry is deliberately not a multiple of 64 bits */
    static const ULONG BITS[] = { 64, 1024, 65536, 1u<<20, 1000003u };
    static const int NB = (int)(sizeof BITS / sizeof BITS[0]);

    printf("%-12s %10s   %10s %10s   %8s   %10s %10s\n",
           "bits", "bytes", "SetAll ns", "ClrAll ns", "Set/Clr", "Set B/ns", "Clr B/ns");
    printf("---------------------------------------------------------------------------------------\n");

    for (int k = 0; k < NB; ++k) {
        ULONG bits = BITS[k];
        size_t bytes = ((size_t)bits + 7) / 8;
        size_t words = (bytes + 3) / 4;
        PULONG buf = (PULONG)VirtualAlloc(NULL, (words + 16) * 4, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
        RTLBM bm; pInit(&bm, buf, bits);

        double s = timeit(pSetAll,   &bm);
        double c = timeit(pClearAll, &bm);
        printf("%-12u %10zu   %10.2f %10.2f   %8.2f   %10.2f %10.2f\n",
               bits, bytes, s, c, c > 0 ? s/c : 0.0, (double)bytes/s, (double)bytes/c);
        VirtualFree(buf, 0, MEM_RELEASE);
    }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
