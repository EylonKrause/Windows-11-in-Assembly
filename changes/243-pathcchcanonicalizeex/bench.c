/* changes/243-pathcchcanonicalizeex/bench.c
   Gate 2: time wia_pathcchcanonicalizeex against the live kernelbase!PathCchCanonicalizeEx.

   NO RESTORE IS NEEDED ANYWHERE, and that is a property of the contract rather than an assumption:
   this function reads its input and writes a SEPARATE output buffer, so no row modifies anything it
   will read again. Change 238's lesson -- that a restore heavier than the function REPLACES the
   measurement -- is the reason to say so explicitly instead of adding a memcpy "to be safe".

   THE SIZE CLASSES STOP AT 250 CHARACTERS, and that is also the contract rather than a choice. With
   dwFlags == 0 the usable buffer is min(cch, 0x104), so a RESULT of 260 characters or more is
   ERROR_FILENAME_EXCED_RANGE however large cch is (RESULTS.md, measured across the boundary). A
   1000-character path is not a valid long input for this function -- it is an error row, and it is
   included as one. The discovery pass that measured 0.317 ns/byte on a 1000-character path was
   therefore timing a walk that stops at the cap.

   THE ROWS vary the three things that decide the cost:

     * THE LENGTH, which both implementations must cross. The shipped code walks it one wchar_t at a
       time with a bounds test per character and an INDIRECT CALL PER COMPONENT to find the component
       end; ours pre-scans with AVX2 and then copies 16 characters per instruction.
     * Whether there are dot components, because that is exactly the condition that decides whether
       our fast path applies. A path with "." or ".." components takes the per-component walk, so it
       is measured separately rather than hidden inside an average.
     * The shape, because the prefix forms and unc change where the walk starts, and a trailing-dot
       path exercises the finish.

   Two error rows are included as the honest floor: a result over the cap, and a cch too small. Both
   return before doing much work, in both implementations.

   Lengths are COMPUTED, never hardcoded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

#define PATHCCH_MAX_CCH 0x8000

extern long wia_pathcchcanonicalizeex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern void wia_pccx_set_fallback(void*);
typedef HRESULT (WINAPI *FN)(PWSTR, size_t, PCWSTR, ULONG);
static FN sys;

typedef struct { const wchar_t* in; size_t cch; wchar_t* out; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)wia_pathcchcanonicalizeex(k->out, k->cch, k->in, 0);
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)sys(k->out, k->cch, k->in, 0);
}
#pragma optimize("", on)

static wchar_t pool[40000];
static wchar_t outbuf[0x8200];
static int pcur;

/* A drive-absolute path of exactly n characters built from `seg`-character components. */
static const wchar_t* mk_plain(int n, int seg)
{
    wchar_t* p = pool + pcur;
    int k = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n) {
        for (int i = 0; i < seg && k < n; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
        if (k < n) p[k++] = L'\\';
    }
    p[n] = 0;
    pcur += n + 8;
    return p;
}

/* A path of exactly n characters in which every third component is `dots` dots. */
static const wchar_t* mk_dots(int n, int seg, int dots)
{
    wchar_t* p = pool + pcur;
    int k = 0, comp = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n) {
        if ((comp % 3) == 2) {
            for (int i = 0; i < dots && k < n; ++i) p[k++] = L'.';
        } else {
            for (int i = 0; i < seg && k < n; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
        }
        ++comp;
        if (k < n) p[k++] = L'\\';
    }
    /* so the row measures the walk and not the finish, do not end on a separator or a dot */
    if (k > 0 && (p[k-1] == L'\\' || p[k-1] == L'.')) p[k-1] = L'z';
    p[n] = 0;
    pcur += n + 8;
    return p;
}

/* A UNC path of exactly n characters. */
static const wchar_t* mk_unc(int n, int seg)
{
    wchar_t* p = pool + pcur;
    int k = 0;
    p[k++] = L'\\'; p[k++] = L'\\';
    for (int i = 0; i < 3 && k < n; ++i) p[k++] = L's';
    if (k < n) p[k++] = L'\\';
    for (int i = 0; i < 3 && k < n; ++i) p[k++] = L'h';
    while (k < n) {
        if (k < n) p[k++] = L'\\';
        for (int i = 0; i < seg && k < n; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
    }
    p[n] = 0;
    pcur += n + 8;
    return p;
}

/* "\\?\C:\..." of exactly n characters. */
static const wchar_t* mk_prefix(int n, int seg)
{
    wchar_t* p = pool + pcur;
    int k = 0;
    p[k++] = L'\\'; p[k++] = L'\\'; p[k++] = L'?'; p[k++] = L'\\';
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n) {
        for (int i = 0; i < seg && k < n; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
        if (k < n) p[k++] = L'\\';
    }
    p[n] = 0;
    pcur += n + 8;
    return p;
}

/* A path of exactly n characters with doubled separators throughout. */
static const wchar_t* mk_doubled(int n, int seg)
{
    wchar_t* p = pool + pcur;
    int k = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n) {
        for (int i = 0; i < seg && k < n; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
        if (k < n) p[k++] = L'\\';
        if (k < n) p[k++] = L'\\';
    }
    p[n] = 0;
    pcur += n + 8;
    return p;
}

/* A path of exactly n characters whose last component ends in dots. */
static const wchar_t* mk_trailing(int n, int seg)
{
    wchar_t* p = pool + pcur;
    int k = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n - 3) {
        for (int i = 0; i < seg && k < n - 3; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
        if (k < n - 3) p[k++] = L'\\';
    }
    while (k < n) p[k++] = L'.';
    p[n] = 0;
    pcur += n + 8;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "PathCchCanonicalizeEx");
    if (!sys) { printf("BENCH SETUP ERROR: cannot resolve PathCchCanonicalizeEx\n"); return 1; }
    wia_pccx_set_fallback((void*)sys);

    enum { N = 14 };
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    static const char* names[N] = {
        "plain 16",
        "plain 32",
        "plain 64",
        "plain 128",
        "plain 250",
        "dotdot 64",
        "dotdot 250",
        "dot 250",
        "unc 128",
        "\\\\?\\ prefix 128",
        "doubled seps 128",
        "trailing dots 128",
        "over the cap (300)",
        "cch too small",
    };
    const wchar_t* ins[N];
    size_t cchs[N];

    ins[0]  = mk_plain(16, 5);      cchs[0]  = PATHCCH_MAX_CCH;
    ins[1]  = mk_plain(32, 5);      cchs[1]  = PATHCCH_MAX_CCH;
    ins[2]  = mk_plain(64, 7);      cchs[2]  = PATHCCH_MAX_CCH;
    ins[3]  = mk_plain(128, 7);     cchs[3]  = PATHCCH_MAX_CCH;
    ins[4]  = mk_plain(250, 7);     cchs[4]  = PATHCCH_MAX_CCH;
    ins[5]  = mk_dots(64, 7, 2);    cchs[5]  = PATHCCH_MAX_CCH;
    ins[6]  = mk_dots(250, 7, 2);   cchs[6]  = PATHCCH_MAX_CCH;
    ins[7]  = mk_dots(250, 7, 1);   cchs[7]  = PATHCCH_MAX_CCH;
    ins[8]  = mk_unc(128, 7);       cchs[8]  = PATHCCH_MAX_CCH;
    ins[9]  = mk_prefix(128, 7);    cchs[9]  = PATHCCH_MAX_CCH;
    ins[10] = mk_doubled(128, 7);   cchs[10] = PATHCCH_MAX_CCH;
    ins[11] = mk_trailing(128, 7);  cchs[11] = PATHCCH_MAX_CCH;
    ins[12] = mk_plain(300, 7);     cchs[12] = PATHCCH_MAX_CCH;
    ins[13] = mk_plain(64, 7);      cchs[13] = 8;

    /* Every row is verified to agree with the live function before it is timed: a benchmark row that
       measures a different answer measures nothing. */
    for (int i = 0; i < N; ++i) {
        static wchar_t a[0x8200], b[0x8200];
        long r1, r2;
        for (int k = 0; k < 64; ++k) { a[k] = 0xCDCD; b[k] = 0xCDCD; }
        r1 = wia_pathcchcanonicalizeex(a, cchs[i], ins[i], 0);
        r2 = (long)sys(b, cchs[i], ins[i], 0);
        if (r1 != r2 || (r1 == 0 && wcscmp(a, b))) {
            printf("BENCH SETUP ERROR: row %d (%s) disagrees: ours %08lX \"%ls\", live %08lX \"%ls\"\n",
                   i, names[i], (unsigned long)r1, a, (unsigned long)r2, b);
            return 1;
        }
        C[i].in = ins[i]; C[i].cch = cchs[i]; C[i].out = outbuf;
        bytes[i] = wcslen(ins[i]) * 2;
        cs[i].label = names[i]; cs[i].bytes = bytes[i]; cs[i].ctx = &C[i];
        cs[i].ours = op_ours; cs[i].system = op_sys;
    }

    return wia_bench_compare("kernelbase PathCchCanonicalizeEx, dwFlags 0 "
                             "(wia AVX2 pre-scan + vector copy vs kernelbase; no restore needed)",
                             cs, N, 300);
}
