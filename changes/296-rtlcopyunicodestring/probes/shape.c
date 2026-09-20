/* changes/296-rtlcopyunicodestring/probes/shape.c
 *
 * THROWAWAY. The first bench run showed the large classes swinging between 0.78x and 1.38x with
 * the SAME code, and the two tables disagreeing at the same length. Two candidate causes, and
 * they call for opposite fixes:
 *
 *   (a) DESTINATION ALIGNMENT. Our 32-byte stores are vmovdqu at whatever address malloc handed
 *       back. A 32-byte store at 16 mod 64 splits a cache line every other block. ntdll's memmove
 *       aligns the destination to 16 before its main loop precisely to avoid that. If this is the
 *       cause, the fix is to align the destination and the penalty will track dst & 31.
 *
 *   (b) 4K ALIASING. A load at src+i is falsely made to wait on an in-flight store at dst+j when
 *       the two share their low 12 address bits, i.e. when (dst - src) mod 4096 is small enough
 *       that the aliasing store is still in the store buffer. That is a property of the DISTANCE
 *       between the two buffers, not of either one's alignment, and no amount of aligning fixes
 *       it. If this is the cause the benchmark's malloc layout is what moved, not the code.
 *
 * So: sweep both axes independently, for our implementation and for the live export, and print
 * GB/s. Whichever axis moves the number is the real one.
 *
 *   BUILD
 *     . .\tools\vsenv.ps1
 *     ml64 /nologo /c changes\296-rtlcopyunicodestring\impl.asm
 *     cl /nologo /O2 changes\296-rtlcopyunicodestring\probes\shape.c impl.obj /Fe:shape.exe && .\shape.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern void wia_copyus(US*, const US*);
typedef void (NTAPI *fn)(US*, const US*);
static fn sys;
static volatile uint64_t sink;

static double freq;
static double ns(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart * 1e9 / freq; }

static double timeit(int useours, unsigned char* d, unsigned char* s, int n)
{
    US src; src.Length = (USHORT)n; src.MaximumLength = (USHORT)n; src.Buffer = (wchar_t*)s;
    double best = 1e30;
    for (int t = 0; t < 30; ++t) {
        double t0 = ns();
        for (int r = 0; r < 500; ++r) {
            US u; u.Length = 0; u.MaximumLength = (USHORT)(n + 2); u.Buffer = (wchar_t*)d;
            if (useours) wia_copyus(&u, &src); else sys(&u, &src);
            sink += u.Length;
        }
        double dt = (ns() - t0) / 500.0;
        if (dt < best) best = dt;
    }
    return (double)n / best;   /* GB/s */
}

int main(void)
{
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    sys = (fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCopyUnicodeString");

    /* One big page-aligned arena so both axes can be set exactly. */
    unsigned char* arena = (unsigned char*)VirtualAlloc(NULL, 1 << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (int i = 0; i < (1 << 20); ++i) arena[i] = (unsigned char)i;

    static const int NS[] = { 508, 2048, 8190 };

    printf("=== axis (a): DESTINATION ALIGNMENT (src fixed page-aligned, dst = arena + 0x40000 + k) ===\n");
    for (int q = 0; q < 3; ++q) {
        int n = NS[q];
        printf("n=%-5d  dst&63:", n);
        for (int k = 0; k < 64; k += 8) printf(" %8d", k);
        printf("\n         ours   :");
        for (int k = 0; k < 64; k += 8) printf(" %8.1f", timeit(1, arena + 0x40000 + k, arena, n));
        printf("\n         ntdll  :");
        for (int k = 0; k < 64; k += 8) printf(" %8.1f", timeit(0, arena + 0x40000 + k, arena, n));
        printf("   GB/s\n");
    }

    printf("\n=== axis (b): SRC->DST DISTANCE mod 4096 (both 64-byte aligned) ===\n");
    for (int q = 0; q < 3; ++q) {
        int n = NS[q];
        static const int D[] = { 64, 256, 512, 1024, 2048, 2112, 3072, 4096, 4160, 8192, 16384, 65536 };
        printf("n=%-5d  dist :", n);
        for (int i = 0; i < 12; ++i) printf(" %7d", D[i]);
        printf("\n         ours :");
        for (int i = 0; i < 12; ++i) printf(" %7.1f", timeit(1, arena + 0x40000 + D[i], arena + 0x40000, n));
        printf("\n         ntdll:");
        for (int i = 0; i < 12; ++i) printf(" %7.1f", timeit(0, arena + 0x40000 + D[i], arena + 0x40000, n));
        printf("   GB/s\n");
    }
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
