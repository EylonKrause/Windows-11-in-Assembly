/* discovery/winrt_string_family.c
   combase's HSTRING functions -- the WinRT string type -- that tools/desktop-surface.py ranks high
   (WindowsCreateStringReference fan-in 113, WindowsGetStringRawBuffer 109, WindowsDeleteString 104,
   WindowsCreateString 92, WindowsIsStringEmpty 82) and that this repository has not touched except
   WindowsCompareStringOrdinal. Most are O(1) header reads; the question is which ones WALK the string
   and what that costs per character, with an allocation-free control next to each.

   Every row prints its result so a row that does no work is visible (discovery/README.md's rule).
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winstring.h>
#include <stdio.h>
#include <stdint.h>

typedef HRESULT (WINAPI *PCSR)(PCWSTR, UINT32, HSTRING_HEADER*, HSTRING*);
typedef HRESULT (WINAPI *PCS)(PCWSTR, UINT32, HSTRING*);
typedef HRESULT (WINAPI *PDS)(HSTRING);
typedef PCWSTR (WINAPI *PRAW)(HSTRING, UINT32*);
typedef UINT32 (WINAPI *PLEN)(HSTRING);
typedef BOOL (WINAPI *PEMPTY)(HSTRING);
typedef HRESULT (WINAPI *PHASNUL)(HSTRING, BOOL*);
typedef HRESULT (WINAPI *PTRIM)(HSTRING, HSTRING, HSTRING*);
typedef HRESULT (WINAPI *PSUB)(HSTRING, UINT32, HSTRING*);
typedef HRESULT (WINAPI *PCAT)(HSTRING, HSTRING, HSTRING*);
typedef HRESULT (WINAPI *PREPL)(HSTRING, HSTRING, HSTRING, HSTRING*);
typedef HRESULT (WINAPI *PDUP)(HSTRING, HSTRING*);

static volatile uint64_t sink;
static double fq;
#define TIME(LABEL, UNITS, BODY) do { \
    double best = 1e30; int inner = 16; \
    for (int w = 0; w < 16; ++w) { BODY; } \
    for (;;) { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); for (int i = 0; i < inner; ++i) { BODY; } QueryPerformanceCounter(&b); \
               double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / fq; if (ns > 200000 || inner > (1 << 22)) break; inner *= 4; } \
    for (int t = 0; t < 30; ++t) { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); for (int i = 0; i < inner; ++i) { BODY; } QueryPerformanceCounter(&b); \
               double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / fq / inner; if (ns < best) best = ns; } \
    printf("  %-46s %9.2f ns  %6.3f ns/char\n", LABEL, best, best / (UNITS)); } while (0)

int main(void) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); fq = (double)f.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(), 4);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    { PROCESS_POWER_THROTTLING_STATE ps = { PROCESS_POWER_THROTTLING_CURRENT_VERSION, PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0 };
      SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ps, sizeof ps); }
    HMODULE c = LoadLibraryW(L"combase.dll");
#define G(T, N) T N = (T)GetProcAddress(c, #N)
    G(PCSR, WindowsCreateStringReference); G(PCS, WindowsCreateString); G(PDS, WindowsDeleteString);
    G(PRAW, WindowsGetStringRawBuffer); G(PLEN, WindowsGetStringLen); G(PEMPTY, WindowsIsStringEmpty);
    G(PHASNUL, WindowsStringHasEmbeddedNull); G(PTRIM, WindowsTrimStringStart); G(PTRIM, WindowsTrimStringEnd);
    G(PSUB, WindowsSubstring); G(PCAT, WindowsConcatString); G(PREPL, WindowsReplaceString); G(PDUP, WindowsDuplicateString);
    static wchar_t s[5000], sp[5000];
    int N[] = { 16, 256, 4096 };
    for (int k = 0; k < 3; ++k) {
        int n = N[k];
        for (int i = 0; i < n; ++i) { s[i] = (wchar_t)(L'a' + i % 26); sp[i] = (i < n / 2) ? L' ' : (wchar_t)(L'a' + i % 26); }
        s[n] = 0; sp[n] = 0;
        printf("\n== %d characters ==\n", n);
        HSTRING_HEADER hh; HSTRING hs = 0, ht = 0, hsp = 0, hspace = 0, ho = 0;
        TIME("WindowsCreateStringReference", n, sink += WindowsCreateStringReference(s, n, &hh, &hs));
        WindowsCreateString(s, n, &ht);
        WindowsCreateString(sp, n, &hsp);
        WindowsCreateString(L" ", 1, &hspace);
        BOOL has = 0;
        TIME("WindowsStringHasEmbeddedNull (none)", n, (sink += WindowsStringHasEmbeddedNull(ht, &has), sink += has));
        printf("     -> has = %d\n", has);
        TIME("WindowsCreateString + Delete", n, { HSTRING x; sink += WindowsCreateString(s, n, &x); WindowsDeleteString(x); });
        TIME("WindowsDuplicateString + Delete (refcount)", n, { HSTRING x; sink += WindowsDuplicateString(ht, &x); WindowsDeleteString(x); });
        TIME("WindowsTrimStringStart, half spaces + Delete", n, { HSTRING x = 0; sink += WindowsTrimStringStart(hsp, hspace, &x); sink += WindowsGetStringLen(x); WindowsDeleteString(x); });
        { HSTRING x = 0; WindowsTrimStringStart(hsp, hspace, &x); printf("     -> trimmed length %u\n", WindowsGetStringLen(x)); WindowsDeleteString(x); }
        TIME("WindowsTrimStringEnd, nothing to trim", n, { HSTRING x = 0; sink += WindowsTrimStringEnd(ht, hspace, &x); WindowsDeleteString(x); });
        TIME("WindowsSubstring(n/2) + Delete", n, { HSTRING x = 0; sink += WindowsSubstring(ht, n / 2, &x); WindowsDeleteString(x); });
        TIME("WindowsConcatString(s, s) + Delete", n, { HSTRING x = 0; sink += WindowsConcatString(ht, ht, &x); WindowsDeleteString(x); });
        { HSTRING ra = 0, rb = 0; WindowsCreateString(L"m", 1, &ra); WindowsCreateString(L"M", 1, &rb);
          TIME("WindowsReplaceString('m' -> 'M') + Delete", n, { HSTRING x = 0; sink += WindowsReplaceString(ht, ra, rb, &x); WindowsDeleteString(x); });
          WindowsDeleteString(ra); WindowsDeleteString(rb); }
        TIME("  control: memcpy of the same", n, { memcpy(s + 2500 > s ? (void*)(sp) : (void*)sp, s, n * 2); sink += sp[0]; });
        UINT32 L = 0; TIME("WindowsGetStringRawBuffer", 1, sink += (uintptr_t)WindowsGetStringRawBuffer(ht, &L));
        TIME("WindowsIsStringEmpty", 1, sink += WindowsIsStringEmpty(ht));
        WindowsDeleteString(ht); WindowsDeleteString(hsp); WindowsDeleteString(hspace);
        (void)ho;
    }
    return 0;
}
