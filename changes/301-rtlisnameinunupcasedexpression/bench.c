// changes/301-rtlisnameinunupcasedexpression/bench.c
// Gate 2: wia_nameinexpr against the live ntdll!RtlIsNameInUnUpcasedExpression, IgnoreCase = FALSE.
//
// Every shape from probes/shapes.c at four name lengths. The rows that decide the verdict are the
// ones where ntdll is ALREADY fast -- "*" (5.8 ns), "*.txt" and its reject (11-13 ns), a prefix
// reject (9.8 ns), "????" (20 ns) and a DOS pattern that fails on its first character (9.8 ns) --
// because those are the cases a heavier general algorithm can lose, and the gate forbids losing
// any. The rows where ntdll walks the name at 7-16 ns per character are where the change earns its
// keep, and they include the DOS forms kernelbase produces from "*.txt" and "*.*".
//
// The name is a realistic file name: "file" + letters + ".txt", so the prefix, suffix and interior
// cases all see the same subject.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
extern BOOLEAN wia_nameinexpr(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD sys;

typedef struct { USTR e, n; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c) { CASE* k = (CASE*)c; return wia_nameinexpr(&k->e, &k->n, FALSE, NULL); }
static uint64_t op_sys (void* c) { CASE* k = (CASE*)c; return sys(&k->e, &k->n, FALSE, NULL); }
#pragma optimize("", on)

int main(void) {
    sys = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");
    if (!sys) { printf("no export\n"); return 2; }

    static const wchar_t* P[] = {
        L"*", L"*.txt", L"*.zzz", L"file*", L"zzzz*", L"*.*", L"????", L"*abc*def*", L"*q*",
        L"<.txt", L"<\"*", L"<", L"*.>>>", L"a<b<c<d",
    };
    enum { NP = sizeof P / sizeof P[0] };
    static const int NLEN[] = { 16, 64, 256, 1024 };
    enum { NN = 4 };

    static wchar_t names[NN][1100];
    for (int k = 0; k < NN; ++k) {
        int n = NLEN[k];
        for (int j = 0; j < n; ++j) names[k][j] = (wchar_t)(L'a' + (j * 7 % 26));
        names[k][n-4] = L'.'; names[k][n-3] = L't'; names[k][n-2] = L'x'; names[k][n-1] = L't';
        names[k][0] = L'f'; names[k][1] = L'i'; names[k][2] = L'l'; names[k][3] = L'e';
    }

    static CASE cs[NP * NN];
    static wia_case wc[NP * NN];
    static char labels[NP * NN][40];
    int m = 0;
    for (int i = 0; i < NP; ++i)
        for (int k = 0; k < NN; ++k) {
            size_t pl = wcslen(P[i]);
            cs[m].e.Length = cs[m].e.MaximumLength = (USHORT)(pl * 2); cs[m].e.Buffer = (PWSTR)P[i];
            cs[m].n.Length = cs[m].n.MaximumLength = (USHORT)(NLEN[k] * 2); cs[m].n.Buffer = names[k];
            sprintf(labels[m], "%ls/%d", P[i], NLEN[k]);
            wc[m].label = labels[m]; wc[m].bytes = (size_t)NLEN[k] * 2;
            wc[m].ours = op_ours; wc[m].system = op_sys; wc[m].ctx = &cs[m];
            ++m;
        }
    return wia_bench_compare("ntdll RtlIsNameInUnUpcasedExpression  (wia: anchored ends + greedy/AVX2 skip + column DP)", wc, m, 300);
}
