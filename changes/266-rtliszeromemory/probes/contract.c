/* changes/266-rtliszeromemory/probes/contract.c
 *
 * What does ntdll!RtlIsZeroMemory actually promise?
 *
 *     BOOLEAN RtlIsZeroMemory(const VOID* Buffer, SIZE_T Length)
 *
 * discovery/ntdll_bitmap3.c measured it at 1618 ns for 64 KB, 0.025 ns/byte, about 40 GB/s, where
 * a VPTEST scan of the same shape measured 125 GB/s in change 259. That is the whole reason it is a
 * target, and it says nothing about the rules, which is what this file is for.
 *
 * a predicate has only two answers, which makes a careless corpus very easy to pass, the lesson
 * change 259 wrote down for RtlAreBitsSet. An implementation that always said NO would agree with
 * the live export on nearly every random buffer, so the questions below are the ones where the two
 * answers are NOT obvious:
 *
 *   * Length 0: vacuously TRUE, or FALSE? Both are defensible and only one is right.
 *   * Is a NULL buffer read at all when the length is zero?
 *   * Does it stop at the first non-zero byte, or read everything regardless? (Timing answers this,
 *     and it decides whether an early exit is worth writing.)
 *   * Does it read PAST the length, rounding up to a vector, say? That would make it unsafe to
 *     call at the end of a page, and it decides whether OUR version may do the same. It cannot be
 *     asked directly, so it is asked with a guard page.
 *   * Is the answer affected by WHERE the non-zero byte is, or by alignment?
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOLEAN (NTAPI *F_IsZero)(const void*, SIZE_T);
static F_IsZero iszero;
static char buf[8192];

static void ask(const char* what, const void* p, SIZE_T n)
{
    printf("  %-56s len=%-6Iu -> %s\n", what, n, iszero(p, n) ? "TRUE" : "false");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    iszero = (F_IsZero)GetProcAddress(h, "RtlIsZeroMemory");
    if (!iszero) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(buf, 0, sizeof buf);

    printf("== RtlIsZeroMemory: the contract ==\n\n");

    printf("-- 1. the ordinary answers --\n");
    ask("8192 zero bytes", buf, 8192);
    buf[0] = 1;   ask("... with a 1 at byte 0", buf, 8192);   buf[0] = 0;
    buf[8191] = 1; ask("... with a 1 at the LAST byte", buf, 8192); buf[8191] = 0;
    buf[4000] = 1; ask("... with a 1 in the middle", buf, 8192); buf[4000] = 0;

    printf("\n-- 2. LENGTH ZERO, and whether the pointer is touched --\n");
    ask("a zero length over a zero buffer", buf, 0);
    buf[0] = 1;
    ask("a zero length over a NON-zero buffer", buf, 0);
    buf[0] = 0;
    {
        BOOLEAN r = iszero(NULL, 0);
        printf("  %-56s len=%-6d -> %s\n", "a NULL pointer with a zero length", 0, r ? "TRUE" : "false");
    }

    printf("\n-- 3. every length from 0 to 80, all-zero and with the LAST byte set --\n");
    {
        int n, bad = 0;
        for (n = 0; n <= 80; ++n) {
            BOOLEAN z, nz;
            memset(buf, 0, 128);
            z = iszero(buf, (SIZE_T)n);
            if (n) buf[n - 1] = 1;
            nz = iszero(buf, (SIZE_T)n);
            if (!z) { printf("   length %d over zeros answered false\n", n); ++bad; }
            if (n && nz) { printf("   length %d with the last byte set answered TRUE\n", n); ++bad; }
        }
        printf("   %s\n", bad ? "see above" :
               "every length 0..80 is TRUE over zeros and false when the LAST byte is set --\n"
               "   so nothing is skipped at the end and the length is respected exactly");
    }

    printf("\n-- 4. a byte set JUST PAST the length must not be seen --\n");
    {
        int n, bad = 0;
        for (n = 0; n <= 80; ++n) {
            BOOLEAN r;
            memset(buf, 0, 128);
            buf[n] = 1;                                 /* one byte past the end of the range */
            r = iszero(buf, (SIZE_T)n);
            if (!r) { printf("   length %d SAW the byte at offset %d\n", n, n); ++bad; }
        }
        printf("   %s\n", bad ? "see above" :
               "no length 0..80 saw the byte one past its end");
    }

    printf("\n-- 5. a GUARD PAGE: does it read past the length? --\n");
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("   SKIPPED\n");
        } else {
            int n, worst = -1;
            for (n = 0; n <= 300; ++n) {
                char* p = base + si.dwPageSize - n;      /* ends exactly at the guard page */
                memset(p, 0, (size_t)n);
                if (!iszero(p, (SIZE_T)n)) { printf("   length %d answered false over zeros\n", n); }
                worst = n;
            }
            printf("   every length 0..%d ending EXACTLY at an inaccessible page: no fault, so it\n"
                   "   does not read past the length and neither may we\n", worst);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    printf("\n-- 6. does it STOP at the first non-zero byte, or read everything? --\n");
    {
        LARGE_INTEGER f, a, b;
        static char big[1 << 20];
        double early, late;
        int i;
        QueryPerformanceFrequency(&f);
        memset(big, 0, sizeof big);
        big[0] = 1;
        QueryPerformanceCounter(&a);
        for (i = 0; i < 20000; ++i) iszero(big, sizeof big);
        QueryPerformanceCounter(&b);
        early = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / 20000.0;
        big[0] = 0; big[(1 << 20) - 1] = 1;
        QueryPerformanceCounter(&a);
        for (i = 0; i < 2000; ++i) iszero(big, sizeof big);
        QueryPerformanceCounter(&b);
        late = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / 2000.0;
        printf("   1 MB with the non-zero byte FIRST: %8.2f ns\n", early);
        printf("   1 MB with the non-zero byte LAST:  %8.2f ns\n", late);
        printf("   %s\n", early * 10.0 < late
               ? "it STOPS at the first non-zero byte -- an early exit is part of the contract"
               : "it reads the whole buffer either way");
    }
    return 0;
}
