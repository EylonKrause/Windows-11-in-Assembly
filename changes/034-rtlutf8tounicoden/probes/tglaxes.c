/* changes/034-rtlutf8tounicoden/probes/tglaxes.c
 *
 * Which (length, alignment) pair is the speed gate for the tiger lake variant, and is any row that
 * looks bad actually 4K ALIASING rather than slow code?
 *
 * This is change 294's probes/blockcount.c question and change 296's probes/twoaxes.c question,
 * asked of `impl_tgl.asm`. Both lessons are in this repository because a bench that measures the
 * (length, alignment) pair malloc happened to hand out reports a number the caller will not get:
 *
 *   * the WIDTH-AGNOSTIC BLOCK reads 64 bytes and consumes a variable number of them, so where the
 *     tail falls relative to 64 moves with the source alignment AND with the class, a block of
 *     three-byte sequences consumes 63 bytes, a block of ASCII consumes all 64;
 *   * change 296 lost two size classes to (dst - src) mod 4096 near zero, where a load stalls
 *     behind an in-flight store that shares its low twelve address bits, and it hits a 64-byte loop
 *     HARDER than a 16-byte one. This decoder's destination is TWICE the size of its source, so the
 *     two cursors sweep past each other and the offset is not constant across the buffer. That is
 *     exactly the shape that produces one inexplicably bad row.
 *
 * So: two axes, measured separately, on the classes that use the new block.
 *   axis 1, source alignment 0..63, destination fixed 64-byte aligned.
 *   axis 2, (dst - src) mod 4096 swept across the danger zone, source fixed.
 * The worst pair of each axis is what bench_tgl.c is then set to use, so the published table is the
 * gate and not the best case.
 *
 * build (from the change directory, after tools/vsenv.ps1):
 *   ml64 /nologo /c /Foprobes\pt.obj impl_tgl.asm
 *   cl /nologo /O2 probes\tglaxes.c probes\pt.obj /Fe:probes\tglaxes.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (WINAPI *fn)(wchar_t*, ULONG, PULONG, const void*, ULONG);
extern NTSTATUS wia_u82u(wchar_t*, ULONG, PULONG, const void*, ULONG);
static fn sys;
static LARGE_INTEGER freq;

/* the same six fills bench_tgl.c uses, by index */
static int fill(int c, unsigned char* s, int n)
{
    int i = 0;
    while (i < n) {
        switch (c) {
        case 0: s[i++] = (unsigned char)('a' + (i & 15)); break;                       /* ascii   */
        case 1: if (i + 1 < n) { s[i++] = (unsigned char)(0xC2 + (i & 15));
                                 s[i++] = (unsigned char)(0x80 + (i & 63)); } else s[i++] = 'z';
                break;                                                                  /* 2-byte  */
        case 2: if (i + 2 < n) { s[i++] = (unsigned char)(0xE1 + (i & 7));
                                 s[i++] = (unsigned char)(0x80 + (i & 63));
                                 s[i++] = (unsigned char)(0x80 + (i & 63)); } else s[i++] = 'z';
                break;                                                                  /* 3-byte  */
        case 3: if (i + 3 < n) { s[i++] = 'a';
                                 s[i++] = 0xE2; s[i++] = 0x82; s[i++] = 0xAC; } else s[i++] = 'z';
                break;                                                                  /* a+3     */
        case 4: if (i + 5 < n) { s[i++] = 'a';
                                 s[i++] = 0xC3; s[i++] = 0xA9;
                                 s[i++] = 0xE2; s[i++] = 0x82; s[i++] = 0xAC; } else s[i++] = 'z';
                break;                                                                  /* a+2+3   */
        default: s[i] = ((i % 32) == 31) ? 0x80 : (unsigned char)('a' + (i & 15)); ++i;
                break;                                                                  /* bad32   */
        }
    }
    return n;
}
static const char* CN[6] = { "ascii", "2byte", "3byte", "a+3", "a+2+3", "bad32" };

static double timeit(int ours, const unsigned char* s, int n, wchar_t* d, ULONG db)
{
    LARGE_INTEGER a, b;
    ULONG got = 0;
    int i, iters = 20000;
    for (i = 0; i < 3000; ++i)
        if (ours) wia_u82u(d, db, &got, s, (ULONG)n); else sys(d, db, &got, s, (ULONG)n);
    QueryPerformanceCounter(&a);
    for (i = 0; i < iters; ++i)
        if (ours) wia_u82u(d, db, &got, s, (ULONG)n); else sys(d, db, &got, s, (ULONG)n);
    QueryPerformanceCounter(&b);
    return (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)freq.QuadPart / iters;
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    unsigned char* sbase = (unsigned char*)_aligned_malloc(65536, 4096);
    unsigned char* dbase = (unsigned char*)_aligned_malloc(262144, 4096);
    int c, off, n = 4000;

    sys = (fn)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    QueryPerformanceFrequency(&freq);
    SetThreadAffinityMask(GetCurrentThread(), 4);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== axis 1: SOURCE ALIGNMENT, n=%d, destination 64-byte aligned ==\n", n);
    printf("%-8s %-28s %10s %10s %8s\n", "class", "src offsets (worst first)", "worst ns", "best ns", "spread");
    for (c = 0; c < 6; ++c) {
        double worst = 0, best = 1e300; int wo = 0, bo = 0;
        for (off = 0; off < 64; ++off) {
            unsigned char* s = sbase + off;
            double t;
            fill(c, s, n);
            t = timeit(1, s, n, (wchar_t*)dbase, 200000);
            if (t > worst) { worst = t; wo = off; }
            if (t < best)  { best  = t; bo = off; }
        }
        printf("%-8s worst off=%-3d best off=%-3d  %10.2f %10.2f %7.3fx\n",
               CN[c], wo, bo, worst, best, worst / best);
    }

    printf("\n== axis 2: (dst - src) mod 4096, n=%d, source 64-byte aligned ==\n", n);
    printf("   a load can stall behind an in-flight store that shares its low 12 address bits.\n");
    printf("%-8s %14s %10s %10s %8s\n", "class", "worst delta", "worst ns", "best ns", "spread");
    for (c = 0; c < 6; ++c) {
        double worst = 0, best = 1e300; int wd = 0;
        int k;
        fill(c, sbase, n);
        for (k = 0; k < 64; ++k) {
            int delta = k * 64;                      /* dst offset inside its 4K page */
            double t = timeit(1, sbase, n, (wchar_t*)(dbase + delta), 200000);
            if (t > worst) { worst = t; wd = delta; }
            if (t < best)  best = t;
        }
        printf("%-8s %14d %10.2f %10.2f %7.3fx\n", CN[c], wd, worst, best, worst / best);
    }

    printf("\n== the worst source alignment, ours vs the live export ==\n");
    printf("%-8s %6s %12s %12s %8s\n", "class", "off", "ours ns", "ntdll ns", "ratio");
    for (c = 0; c < 6; ++c) {
        double worstr = 1e300; int wo = 0;
        for (off = 0; off < 64; ++off) {
            unsigned char* s = sbase + off;
            double o, t;
            fill(c, s, n);
            o = timeit(1, s, n, (wchar_t*)dbase, 200000);
            t = timeit(0, s, n, (wchar_t*)dbase, 200000);
            if (t / o < worstr) { worstr = t / o; wo = off; }
        }
        {
            unsigned char* s = sbase + wo;
            double o, t;
            fill(c, s, n);
            o = timeit(1, s, n, (wchar_t*)dbase, 200000);
            t = timeit(0, s, n, (wchar_t*)dbase, 200000);
            printf("%-8s %6d %12.2f %12.2f %7.2fx\n", CN[c], wo, o, t, t / o);
        }
    }
    return 0;
}
