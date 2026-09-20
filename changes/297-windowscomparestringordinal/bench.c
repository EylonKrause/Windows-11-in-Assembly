// changes/297-windowscomparestringordinal/bench.c
// Gate 2: time wia_WindowsCompareStringOrdinal against the LIVE combase!WindowsCompareStringOrdinal.
//
// Equal strings are the worst case and most of the table is built from them: the comparison has to
// reach the end before it can answer. The two "differ at the midpoint" rows are here because the
// shipped export stops at the first difference too, and a table made only of equal strings would
// not say whether we still win when neither side has to run to the end. A "differ at index 0" row
// is deliberately NOT here: it measures the call and nothing else, and probes/wcso.c already
// reports it (5.7-6.2 ns flat across every length, on both sides).
//
// The handles are real, not forged. correctness.c forges headers because that is the only way to
// reach a page boundary and the NULL-buffer corners; a benchmark has no such need, so every string
// here comes from WindowsCreateStringReference, the fast-pass handle a WinRT caller actually
// passes, and the one that does NOT copy its buffer.
//
// Short rows are timed x16, which is change 261's remedy for this harness's own floor: an empty
// call through wia_bench_compare costs 2.32 ns on this machine, and a four-character comparison is
// well under that, so a x1 row would be mostly harness. The x16 rows report the time for sixteen
// calls and their GB/s is scaled to match.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

extern HRESULT wia_WindowsCompareStringOrdinal(void*, void*, INT32*);
typedef HRESULT (WINAPI *FN)(void*, void*, INT32*);
typedef HRESULT (WINAPI *CREATEREF)(const wchar_t*, UINT32, void*, void**);
static FN sys;

typedef struct { void* a; void* b; int reps; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; uint64_t s=0; INT32 r; int i;
    for (i = 0; i < k->reps; ++i) { wia_WindowsCompareStringOrdinal(k->a, k->b, &r); s += (uint64_t)(unsigned)r; }
    return s; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; uint64_t s=0; INT32 r; int i;
    for (i = 0; i < k->reps; ++i) { sys(k->a, k->b, &r); s += (uint64_t)(unsigned)r; }
    return s; }
#pragma optimize("", on)

/* The buffers are heap-allocated on purpose. Declaring a dozen multi-kilobyte statics moves every
   array declared after them and shifts rows that did not change at all -- the 4K-aliasing family
   that parked changes 142, 228, 230 and 241, and that moved one of change 210's rows by 2x when a
   local was added to its main(). */
enum { N = 15 };
static CASE C[N];
static wia_case cs[N];
static char hdrs[2 * N][24];

int main(void)
{
    HMODULE cb = LoadLibraryW(L"combase.dll");
    CREATEREF CreateRef = (CREATEREF)GetProcAddress(cb, "WindowsCreateStringReference");
    sys = (FN)GetProcAddress(cb, "WindowsCompareStringOrdinal");

    static const int   L[N]     = { 1, 2, 4, 8, 13, 16, 32, 64, 128, 254, 1024, 4000, 254, 4000, 0 };
    static const int   diff[N]  = { 0, 0, 0, 0,  0,  0,  0,  0,   0,   0,    0,    0,   1,    1, 0 };
    static const char* names[N] = {
        "1 char (x16)", "2 chars (x16)", "4 chars (x16)", "8 chars (x16)", "13 chars (x16)",
        "16 chars (x16)", "32 chars (x16)", "64 chars (x16)", "128 chars", "254 chars",
        "1024 chars", "4000 chars", "254, differ at mid", "4000, differ at mid",
        "NULL vs NULL (x16)" };
    int i;

    for (i = 0; i < N; ++i) {
        int n = L[i];
        void *s1 = 0, *s2 = 0;
        if (n == 0 && i == N - 1) {                       /* both handles NULL: the early-out */
            C[i].a = 0; C[i].b = 0;
        } else {
            wchar_t* a = (wchar_t*)malloc(((size_t)n + 1) * 2);
            wchar_t* b = (wchar_t*)malloc(((size_t)n + 1) * 2);
            int k;
            for (k = 0; k < n; ++k) { a[k] = (wchar_t)(L'a' + (k % 26)); b[k] = a[k]; }
            if (diff[i] && n) b[n / 2] = (wchar_t)(a[n / 2] ^ 1);
            a[n] = 0; b[n] = 0;
            CreateRef(a, (UINT32)n, hdrs[2 * i], &s1);
            CreateRef(b, (UINT32)n, hdrs[2 * i + 1], &s2);
            C[i].a = s1; C[i].b = s2;
        }
        C[i].reps = (L[i] <= 64) ? 16 : 1;
        cs[i].label  = names[i];
        cs[i].bytes  = (size_t)L[i] * 2 * (size_t)C[i].reps;
        cs[i].ours   = op_ours;
        cs[i].system = op_sys;
        cs[i].ctx    = &C[i];
    }
    return wia_bench_compare("combase WindowsCompareStringOrdinal (wia AVX2 vs the shipped shim)",
                             cs, N, 300);
}
