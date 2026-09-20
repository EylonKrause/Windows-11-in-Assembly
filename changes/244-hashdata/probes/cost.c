/* changes/244-hashdata/probes/cost.c
 *
 * Where the shipped cost actually lives, per source byte and per digest byte.
 *
 * The discovery sweep timed one shape only -- 4096 source bytes into a 16-byte digest, 6.37 ns per
 * source byte. That single number cannot say whether the cost is proportional to cbData x cbHash
 * (a table lookup per pair, which is what the algorithm needs) or whether small digests are
 * LATENCY-bound instead, which is the case that decides whether a faster implementation is
 * possible at all: at cbHash == 1 there is exactly one chain and no parallelism to exploit, so a
 * rewrite can only win by taking the chain out of memory and into a register.
 *
 * This matters because the gate is per size class. An implementation that is four times faster at
 * cbHash == 16 and half as fast at cbHash == 2 does not land, and cbHash == 2 is not a shape the
 * benchmark may quietly omit.
 *
 * Reported per shape: total ns, ns per source byte, and ns per (source byte x digest byte), which
 * is the cost of ONE table lookup. If that last column is flat, the cost is the lookups; if it
 * rises as cbHash falls, the small digests are latency-bound.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef HRESULT (WINAPI *FN_HASH)(const BYTE*, DWORD, BYTE*, DWORD);
static FN_HASH sys;
static LARGE_INTEGER F;
static volatile uint64_t sink;

static double timeit(const BYTE* s, DWORD n, BYTE* d, DWORD m, int reps)
{
    LARGE_INTEGER a, b;
    double best = 1e300;
    for (int i = 0; i < 8; ++i) sink += sys(s, n, d, m);
    for (int t = 0; t < 9; ++t) {
        QueryPerformanceCounter(&a);
        for (int i = 0; i < reps; ++i) sink += sys(s, n, d, m);
        QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)F.QuadPart / (double)reps;
        if (ns < best) best = ns;
    }
    return best;
}

int main(void)
{
    static BYTE src[8192], dig[512];
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN_HASH)GetProcAddress(h, "HashData");
    if (!sys) { printf("cannot resolve HashData\n"); return 1; }
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for (int i = 0; i < 8192; ++i) src[i] = (BYTE)(i * 31 + 7);

    static const DWORD MS[] = { 1, 2, 3, 4, 6, 8, 12, 16, 20, 24, 32, 48, 64, 128, 256 };
    static const DWORD NS[] = { 16, 64, 256, 4096 };
    printf("shipped HashData: cost per shape (min of nine batches, core 2, HIGH priority)\n\n");
    printf("%8s %8s %12s %12s %14s\n", "cbData", "cbHash", "ns", "ns/srcbyte", "ns/lookup");
    for (int ni = 0; ni < (int)(sizeof NS / sizeof NS[0]); ++ni) {
        for (int mi = 0; mi < (int)(sizeof MS / sizeof MS[0]); ++mi) {
            DWORD n = NS[ni], m = MS[mi];
            int reps = (int)(4000000 / (n * (m < 4 ? 4 : m)) + 200);
            if (reps > 200000) reps = 200000;
            double ns = timeit(src, n, dig, m, reps);
            printf("%8lu %8lu %12.2f %12.4f %14.4f\n",
                   (unsigned long)n, (unsigned long)m, ns, ns / (double)n,
                   ns / ((double)n * (double)m));
        }
        printf("\n");
    }
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
