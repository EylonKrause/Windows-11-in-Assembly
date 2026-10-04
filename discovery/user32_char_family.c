/* discovery/user32_char_family.c
   user32's character-conversion exports that tools/uncovered-exports.py still lists, timed per
   character against the ntdll primitive that does the same job (and which this repo has converted):

     CharUpperA / CharLowerA (string mode), CharUpperBuffA / CharLowerBuffA   -- the ANSI case maps
     CharToOemW / CharToOemBuffW, OemToCharW / OemToCharBuffW               -- UTF-16 <-> OEM code page
     CharToOemA / OemToCharA                                                 -- ANSI <-> OEM, both 8-bit
     CharNextW / CharPrevW                                                   -- one step, for scale

   WHY. Change 302 found the W case mappers walking a string twice at ~0.8 ns/char; 277 had already
   beaten the Buff forms. The A forms go through the ANSI code page, which is either a table (cheap,
   convertible) or a round trip through MultiByteToWideChar (expensive, maybe still convertible). The
   OEM converters are the same question for the OEM code page: ntdll has RtlUnicodeToOemN (028) and
   RtlOemToUnicodeN (029) -- if user32 is a thin wrapper over them, the gap is its own overhead.

   Every row prints the per-character cost; the controls print alongside. Nothing is written outside
   this process's own buffers.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef LONG (NTAPI *PU2O)(PCHAR, ULONG, PULONG, PCWCH, ULONG);
typedef LONG (NTAPI *PO2U)(PWCH, ULONG, PULONG, const CHAR*, ULONG);

static volatile uint64_t sink;
static double fq;

#define TIME(LABEL, UNITS, BODY) do { \
    double best = 1e30; int inner = 16; \
    for (int w = 0; w < 16; ++w) { BODY; } \
    for (;;) { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); for (int i = 0; i < inner; ++i) { BODY; } QueryPerformanceCounter(&b); \
               double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / fq; if (ns > 200000 || inner > (1 << 22)) break; inner *= 4; } \
    for (int t = 0; t < 30; ++t) { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); for (int i = 0; i < inner; ++i) { BODY; } QueryPerformanceCounter(&b); \
               double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / fq / inner; if (ns < best) best = ns; } \
    printf("  %-40s %10.2f ns   %6.3f ns/char\n", LABEL, best, best / (UNITS)); } while (0)

int main(void) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); fq = (double)f.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(), 4);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    { PROCESS_POWER_THROTTLING_STATE ps = { PROCESS_POWER_THROTTLING_CURRENT_VERSION, PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0 };
      SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ps, sizeof ps); }
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    PU2O u2o = (PU2O)GetProcAddress(nt, "RtlUnicodeToOemN");
    PO2U o2u = (PO2U)GetProcAddress(nt, "RtlOemToUnicodeN");
    printf("ACP %u, OEMCP %u\n", GetACP(), GetOEMCP());
    static char sa[5000], ta[5000], oa[5000]; static wchar_t sw[5000], tw[5000];
    int N[] = { 16, 256, 4096 };
    for (int k = 0; k < 3; ++k) {
        int n = N[k];
        printf("\n== %d characters ==\n", n);
        for (int i = 0; i < n; ++i) { sa[i] = (char)('a' + i % 26); sw[i] = (wchar_t)(L'a' + i % 26); }
        sa[n] = 0; sw[n] = 0;
        /* case maps, A: map a copy so every call does the same work */
        TIME("CharUpperA (string), lowercase input", n, (memcpy(ta, sa, n + 1), sink += (uintptr_t)CharUpperA(ta)));
        TIME("CharLowerA (string), lowercase input", n, (memcpy(ta, sa, n + 1), sink += (uintptr_t)CharLowerA(ta)));
        TIME("CharUpperBuffA", n, (memcpy(ta, sa, n + 1), sink += CharUpperBuffA(ta, n)));
        TIME("CharLowerBuffA", n, (memcpy(ta, sa, n + 1), sink += CharLowerBuffA(ta, n)));
        TIME("  control: memcpy of the same", n, (memcpy(ta, sa, n + 1), sink += ta[0]));
        TIME("CharUpperW (string), for scale", n, (memcpy(tw, sw, (n + 1) * 2), sink += (uintptr_t)CharUpperW(tw)));
        /* OEM */
        TIME("CharToOemW", n, sink += CharToOemW(sw, oa));
        TIME("CharToOemBuffW", n, sink += CharToOemBuffW(sw, oa, n));
        TIME("  control: RtlUnicodeToOemN", n, { ULONG r; sink += u2o(oa, n, &r, sw, n * 2); });
        TIME("OemToCharW", n, sink += OemToCharW(sa, tw));
        TIME("OemToCharBuffW", n, sink += OemToCharBuffW(sa, tw, n));
        TIME("  control: RtlOemToUnicodeN", n, { ULONG r; sink += o2u(tw, n * 2, &r, sa, n); });
        TIME("CharToOemA", n, sink += CharToOemA(sa, oa));
        TIME("OemToCharA", n, sink += OemToCharA(sa, oa));
        TIME("CharToOemBuffA", n, sink += CharToOemBuffA(sa, oa, n));
        TIME("OemToCharBuffA", n, sink += OemToCharBuffA(sa, oa, n));
    }
    printf("\n== one step ==\n");
    TIME("CharNextW", 1, sink += (uintptr_t)CharNextW(sw));
    TIME("CharPrevW", 1, sink += (uintptr_t)CharPrevW(sw, sw + 5));
    TIME("CharNextA", 1, sink += (uintptr_t)CharNextA(sa));
    return 0;
}
