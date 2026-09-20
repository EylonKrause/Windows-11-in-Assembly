/* changes/240-pathcchremovefilespec/bench.c
   Gate 2: time wia_pathcchremovefilespec against the live kernelbase!PathCchRemoveFileSpec.

   THE RESTORE IS PAID ONLY WHERE IT IS NEEDED. A call that removes something modifies the buffer and
   must be restored every iteration, both sides paying that memcpy. A call that returns S_FALSE writes
   NOTHING AT ALL -- correctness.c proves it over millions of cases with a poison window -- so a
   restore there would undo nothing, and including one does not add noise, IT REPLACES THE
   MEASUREMENT. Change 238's first benchmark reported a refusal at 1.04x for exactly that reason: a
   4000-byte memcpy racing a 4000-byte memcpy while the function it was supposed to be timing cost
   about one nanosecond. The setup below ASSERTS that each no-op row really leaves its buffer
   byte-identical rather than assuming it.

   THE CASE MIX. discovery/kernelbase_pathcch.c measured the shipped function at 0.348 ns per byte on
   a 1000-character path, which is the profile of a forward per-character walk that tracks the last
   separator as it goes. This implementation does the opposite -- one vectorised wcslen, then a
   BACKWARD vectorised scan that finds the last separator in its first 32-byte block -- so the rows
   vary the two things that decide the cost:

     * The length of the path, which is what the wcslen must cross either way;
     * The length of the last component, which is all the backward scan has to cross. a long path
       with a short final component is the shape every real path has, and it is where the difference
       between walking forwards and walking backwards shows up.

   A no-op row (a path that is already its own root) is included as the honest floor, and a
   too-small-cch row because that path returns before doing any work at all.

   Lengths are COMPUTED, never hardcoded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

#define PATHCCH_MAX_CCH 0x8000

extern HRESULT wia_pathcchremovefilespec(wchar_t*, size_t);
typedef HRESULT (WINAPI *FN)(PWSTR, size_t);
static FN sys;

typedef struct { const wchar_t* src; size_t n; size_t cch; wchar_t* work; } CASE;

#pragma optimize("", off)
/* the buffer changes: both sides pay the restore */
static uint64_t op_ours_r(void* c){
    CASE* k = (CASE*)c;
    memcpy(k->work, k->src, (k->n + 1) * 2);
    return (uint64_t)wia_pathcchremovefilespec(k->work, k->cch);
}
static uint64_t op_sys_r(void* c){
    CASE* k = (CASE*)c;
    memcpy(k->work, k->src, (k->n + 1) * 2);
    return (uint64_t)sys(k->work, k->cch);
}
/* nothing is written: no restore, and no memcpy to hide behind */
static uint64_t op_ours_n(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)wia_pathcchremovefilespec(k->work, k->cch);
}
static uint64_t op_sys_n(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)sys(k->work, k->cch);
}
#pragma optimize("", on)

static wchar_t pool[40000];
static wchar_t work[8192];
static wchar_t nobuf[4][8192];

/* An absolute path of n characters whose LAST component is `tail` characters long. */
static const wchar_t* mk(int off, int n, int tail)
{
    wchar_t* p = pool + off;
    int k = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    int body = n - tail - 1;
    while (k < body) {
        for (int i = 0; i < 7 && k < body; ++i) p[k++] = (wchar_t)(L'a' + i);
        if (k < body) p[k++] = L'\\';
    }
    if (k < n) p[k++] = L'\\';
    while (k < n) p[k++] = L'z';
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "PathCchRemoveFileSpec");

    enum { N = 8 };
    /* length, last-component length, and a flag for the rows that write nothing */
    static const int LEN [N] = {  16,   64,  260, 1000, 4000, 1000,    9,   16 };
    static const int TAIL[N] = {   7,    7,    7,    7,    7,  200,   -1,   -1 };
    static const char* names[N] = {
        "16, tail 7",
        "64, tail 7",
        "260, tail 7",
        "1000, tail 7",
        "4000, tail 7",
        "1000, tail 200",
        "no-op (own root)",
        "cch too small",
    };
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int cur = 0, nn = 0;
        for (int i = 0; i < N; ++i) {
            if (TAIL[i] >= 0) {
                C[i].src = mk(cur, LEN[i], TAIL[i]);
                C[i].n = (size_t)LEN[i];
                C[i].cch = PATHCCH_MAX_CCH;
                C[i].work = work;
                cur += LEN[i] + 32;
                if (cur > 38000) { printf("BENCH SETUP ERROR: pool overflow\n"); return 1; }
                bytes[i] = (size_t)LEN[i] * 2;
                cs[i].ours = op_ours_r; cs[i].system = op_sys_r;
            } else {
                /* rows that write nothing: an own-root path, and a cch that is refused outright */
                wchar_t* w = nobuf[nn++];
                if (i == 6) { wcscpy(w, L"C:\\dir\\a"); w[3]=L'd'; wcscpy(w, L"\\\\srv\\shr");
                              C[i].cch = PATHCCH_MAX_CCH; }
                else        { wcscpy(w, L"C:\\dir\\file.txt"); C[i].cch = 2; }
                C[i].src = w;
                C[i].n = wcslen(w);
                C[i].work = w;
                bytes[i] = C[i].n * 2;
                /* ASSERT the row really writes nothing, so dropping the restore is sound */
                static wchar_t copy[64];
                memcpy(copy, w, (C[i].n + 1) * 2);
                HRESULT r1 = wia_pathcchremovefilespec(w, C[i].cch);
                HRESULT r2 = sys(w, C[i].cch);
                if (r1 != r2 || memcmp(copy, w, (C[i].n + 1) * 2) != 0) {
                    printf("BENCH SETUP ERROR: row %d is not a clean no-op "
                           "(ours %08lX, live %08lX, buffer %s)\n", i,
                           (unsigned long)r1, (unsigned long)r2,
                           memcmp(copy, w, (C[i].n + 1) * 2) ? "MODIFIED" : "intact");
                    return 1;
                }
                cs[i].ours = op_ours_n; cs[i].system = op_sys_n;
            }
            cs[i].label = names[i]; cs[i].bytes = bytes[i]; cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("kernelbase PathCchRemoveFileSpec (wia AVX2 wcslen + BACKWARD scan vs "
                             "kernelbase; no-op rows pay no restore)", cs, N, 300);
}
