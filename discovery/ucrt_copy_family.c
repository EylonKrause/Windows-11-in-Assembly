/* discovery/ucrt_copy_family.c
   The ucrtbase copy/append family that tools/uncovered-exports.py still lists: strcpy, wcscpy,
   strncpy, wcsncpy, strcat, wcscat, strncat, wcsncat, and the bounded lengths strnlen, wcsnlen.

   WHY. Every scan-shaped CRT function was converted early (001-046); the copies were left alone on an
   old note that they are "memory-bound". A copy of a 4096-byte string that stays in L1 is not memory
   bound -- the right comparison is the C runtime's own memcpy of the same bytes, which this prints
   next to each. A function within ~2x of memcpy is not a target; one at 5-10x is a byte loop.

   Each is timed at 16, 256 and 4096 characters, destination restored where the call appends.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef char*    (__cdecl *PSCPY)(char*, const char*);
typedef wchar_t* (__cdecl *PWCPY)(wchar_t*, const wchar_t*);
typedef char*    (__cdecl *PSNCPY)(char*, const char*, size_t);
typedef wchar_t* (__cdecl *PWNCPY)(wchar_t*, const wchar_t*, size_t);
typedef size_t   (__cdecl *PSNLEN)(const char*, size_t);
typedef size_t   (__cdecl *PWNLEN)(const wchar_t*, size_t);
typedef void*    (__cdecl *PMEMCPY)(void*, const void*, size_t);

static volatile uint64_t sink;
static double fq;

#define TIME(LABEL, UNITS, BODY) do { \
    double best = 1e30; int inner = 64; \
    for (int w = 0; w < 64; ++w) { BODY; } \
    for (;;) { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); for (int i = 0; i < inner; ++i) { BODY; } QueryPerformanceCounter(&b); \
               double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / fq; if (ns > 200000 || inner > (1 << 22)) break; inner *= 4; } \
    for (int t = 0; t < 30; ++t) { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); for (int i = 0; i < inner; ++i) { BODY; } QueryPerformanceCounter(&b); \
               double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / fq / inner; if (ns < best) best = ns; } \
    printf("  %-34s %9.2f ns   %6.3f ns/unit\n", LABEL, best, best / (UNITS)); } while (0)

int main(void) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); fq = (double)f.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(), 4);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    { PROCESS_POWER_THROTTLING_STATE ps = { PROCESS_POWER_THROTTLING_CURRENT_VERSION, PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0 };
      SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ps, sizeof ps); }
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    PSCPY strcpy_ = (PSCPY)GetProcAddress(u, "strcpy"), strcat_ = (PSCPY)GetProcAddress(u, "strcat");
    PWCPY wcscpy_ = (PWCPY)GetProcAddress(u, "wcscpy"), wcscat_ = (PWCPY)GetProcAddress(u, "wcscat");
    PSNCPY strncpy_ = (PSNCPY)GetProcAddress(u, "strncpy"), strncat_ = (PSNCPY)GetProcAddress(u, "strncat");
    PWNCPY wcsncpy_ = (PWNCPY)GetProcAddress(u, "wcsncpy"), wcsncat_ = (PWNCPY)GetProcAddress(u, "wcsncat");
    PSNLEN strnlen_ = (PSNLEN)GetProcAddress(u, "strnlen"); PWNLEN wcsnlen_ = (PWNLEN)GetProcAddress(u, "wcsnlen");
    PMEMCPY memcpy_ = (PMEMCPY)GetProcAddress(u, "memcpy");
    static char sa[5000], da[10000]; static wchar_t sw[5000], dw[10000];
    int N[] = { 16, 256, 4096 };
    for (int k = 0; k < 3; ++k) {
        int n = N[k]; char lab[64];
        memset(sa, 'a', n); sa[n] = 0;
        for (int i = 0; i < n; ++i) sw[i] = L'a'; sw[n] = 0;
        printf("\n== %d units ==\n", n);
        sprintf(lab, "memcpy %d bytes (control)", n + 1);           TIME(lab, n, sink += (uintptr_t)memcpy_(da, sa, n + 1));
        sprintf(lab, "memcpy %d bytes (control, wide)", 2 * n + 2); TIME(lab, n, sink += (uintptr_t)memcpy_(dw, sw, 2 * n + 2));
        TIME("strcpy", n, sink += (uintptr_t)strcpy_(da, sa));
        TIME("wcscpy", n, sink += (uintptr_t)wcscpy_(dw, sw));
        TIME("strncpy (n = len + 1)", n, sink += (uintptr_t)strncpy_(da, sa, n + 1));
        TIME("wcsncpy (n = len + 1)", n, sink += (uintptr_t)wcsncpy_(dw, sw, n + 1));
        TIME("strncpy (n = 2 len: NUL fill)", n, sink += (uintptr_t)strncpy_(da, sa, 2 * n));
        TIME("wcsncpy (n = 2 len: NUL fill)", n, sink += (uintptr_t)wcsncpy_(dw, sw, 2 * n));
        TIME("strcat onto 16", n, (da[16] = 0, sink += (uintptr_t)strcat_(da, sa)));
        TIME("wcscat onto 16", n, (dw[16] = 0, sink += (uintptr_t)wcscat_(dw, sw)));
        TIME("strncat onto 16 (n = len)", n, (da[16] = 0, sink += (uintptr_t)strncat_(da, sa, n)));
        TIME("wcsncat onto 16 (n = len)", n, (dw[16] = 0, sink += (uintptr_t)wcsncat_(dw, sw, n)));
        TIME("strnlen (max = len + 8)", n, sink += strnlen_(sa, n + 8));
        TIME("wcsnlen (max = len + 8)", n, sink += wcsnlen_(sw, n + 8));
    }
    return 0;
}
