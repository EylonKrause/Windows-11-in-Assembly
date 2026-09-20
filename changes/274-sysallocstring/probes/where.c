/* changes/274-sysallocstring/probes/where.c
 *
 * Is the time really the length scan, and is there anything left to win?
 *
 * probes/contract.c settled the shape: a BSTR's allocator is PRIVATE to oleaut32 -- a block made by
 * hand through CoTaskMemAlloc fail-fasts when SysFreeString touches it, and so does
 * CoTaskMemRealloc on a real one -- so no implementation outside oleaut32 can produce a block the
 * caller may free. It also established that SysAllocString(s) is byte-for-byte
 * SysAllocStringLen(s, wcslen(s)) over fourteen lengths.
 *
 * So the only thing this change can own is the LENGTH SCAN, and whether that is worth owning is a
 * number, not an opinion. discovery/sid_inet_bstr.c put SysAllocString at 838.28 ns on 8000 bytes
 * and SysAllocStringLen at 65.65 ns, which would make the scan 773 ns for 4000 characters -- about
 * 0.19 ns per character, roughly one character per cycle, which is what a byte-at-a-time loop
 * costs. Change 001's wcslen runs at about 0.03 ns per character.
 *
 * This file checks that before a line of assembly is written, because if the gap is really the
 * allocator rather than the scan there is nothing here and the change should not exist. Three
 * things are timed at every length:
 *
 *     SysAllocString(s)                      what we are replacing
 *     SysAllocStringLen(s, lstrlenW(s))      the same work with the OS's own scan factored out
 *     SysAllocStringLen(s, n)                the allocator alone, with the length already known
 *
 * The third is the FLOOR: no implementation of this export can beat it, because the allocation is
 * not ours to make. If the first and the third are close at short lengths, the short rows cannot
 * improve and the bench has to say so rather than hiding them.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>

#pragma comment(lib, "oleaut32.lib")

static double freq;
static volatile unsigned long long sink;

#define REP 2000

static double t_alloc(const wchar_t* s, int n, int mode)
{
    LARGE_INTEGER a, b;
    double best = 1e300;
    int pass, i;
    for (pass = 0; pass < 150; ++pass) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < REP; ++i) {
            BSTR t;
            if (mode == 0)      t = SysAllocString(s);
            else if (mode == 1) t = SysAllocStringLen(s, (UINT)lstrlenW(s));
            else                t = SysAllocStringLen(s, (UINT)n);
            sink += (unsigned long long)(UINT_PTR)t;
            SysFreeString(t);
        }
        QueryPerformanceCounter(&b);
        {
            double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq / REP;
            if (ns < best) best = ns;
        }
    }
    return best;
}

int main(void)
{
    LARGE_INTEGER f;
    static wchar_t buf[70000];
    static const int LENS[] = { 0, 1, 4, 8, 16, 32, 64, 128, 256, 1000, 4000, 16000, 65000 };
    unsigned i;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;

    printf("== where SysAllocString's time goes (nanoseconds, min of 150 passes of 2000) ==\n");
    printf("   %8s  %12s  %14s  %14s   %8s\n",
           "chars", "SysAllocStr", "Len+lstrlenW", "Len, n known", "scan");
    for (i = 0; i < sizeof LENS / sizeof LENS[0]; ++i) {
        int n = LENS[i], k;
        double t0, t1, t2;
        for (k = 0; k < n; ++k) buf[k] = (wchar_t)(L'a' + (k % 26));
        buf[n] = 0;
        t0 = t_alloc(buf, n, 0);
        t1 = t_alloc(buf, n, 1);
        t2 = t_alloc(buf, n, 2);
        printf("   %8d  %12.2f  %14.2f  %14.2f   %8.2f\n", n, t0, t1, t2, t0 - t2);
    }

    printf("\n== and what that scan costs per character ==\n");
    {
        int n = 65000, k;
        double t0, t2;
        for (k = 0; k < n; ++k) buf[k] = (wchar_t)(L'a' + (k % 26));
        buf[n] = 0;
        t0 = t_alloc(buf, n, 0);
        t2 = t_alloc(buf, n, 2);
        printf("   %.4f ns per character over %d characters\n", (t0 - t2) / n, n);
        printf("   change 001's wcslen runs at roughly 0.03 ns per character\n");
    }
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
