/* discovery/cmpordinal_foldpath.c
 *
 * A ROW CAN BE NAMED FOR A PATH IT DOES NOT REACH.
 *
 * Change 210 (kernelbase!CompareStringOrdinal) benches its ignore-case mode in four rows, one of
 * them labelled "4000 Cyrillic, ci (table path)", and its bench.c says in as many words:
 *
 *     "The NON-ASCII class exists precisely to keep that fallback honest -- an implementation that
 *      only ever benchmarked ASCII would hide it."
 *
 * The intent is right and the row does not do it. Every pair in that benchmark is built as
 * `BU[i] = AU[i]` -- the two strings are IDENTICAL -- and the implementation's first tier is
 *
 *     equal raw  =>  equal folded, in any alphabet  =>  advance 16
 *
 * which fires before the 0x7F test that would send the chunk to the table. So the row measures tier
 * one on Cyrillic input, not the table path, and the fallback it exists to keep honest has never
 * been timed.
 *
 * THIS FILE TIMES IT, by comparing strings that differ. The classes are chosen so that each one
 * reaches a different tier, and the identical-string rows are kept alongside as the control that
 * reproduces the published figures:
 *
 *   ascii same      identical ASCII                       tier 1, and the published row
 *   ascii case      ASCII differing only in CASE          tier 2: the in-register fold
 *   ascii differ    ASCII differing in the last character tier 2 then a verdict
 *   cyr same        identical Cyrillic                    tier 1 -- what the "table path" row does
 *   cyr case        Cyrillic differing only in case       TIER 3: the 64K table, at last
 *   cyr differ      Cyrillic differing in the last char   tier 3 then a verdict
 *   mixed case      ASCII and Cyrillic interleaved, case-differing
 *
 * It is evidence, not a fix.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

extern int wia_comparestringordinal(const wchar_t*, int, const wchar_t*, int, BOOL);
extern void wia_upcase_init(void);
typedef int (WINAPI *FN)(LPCWCH, int, LPCWCH, int, BOOL);
static FN sys;

typedef struct { const wchar_t* a; const wchar_t* b; int n; int ic; } CASE;
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)wia_comparestringordinal(k->a,k->n,k->b,k->n,k->ic); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)sys(k->a,k->n,k->b,k->n,k->ic); }

enum { CLASSES = 7, LENS = 3 };
static const char* CNAME[CLASSES] = {
    "ascii same", "ascii case", "ascii differ",
    "cyr same", "cyr case", "cyr differ", "mixed case"
};

/* lower/upper pairs: ASCII a..z / A..Z, Cyrillic U+0430.. / U+0410.. */
static void build(int cls, wchar_t* a, wchar_t* b, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        switch (cls) {
        case 0: a[i] = b[i] = (wchar_t)(L'a' + (i % 26)); break;
        case 1: a[i] = (wchar_t)(L'a' + (i % 26)); b[i] = (wchar_t)(L'A' + (i % 26)); break;
        case 2: a[i] = b[i] = (wchar_t)(L'a' + (i % 26)); break;
        case 3: a[i] = b[i] = (wchar_t)(0x0430 + (i % 26)); break;
        case 4: a[i] = (wchar_t)(0x0430 + (i % 26)); b[i] = (wchar_t)(0x0410 + (i % 26)); break;
        case 5: a[i] = b[i] = (wchar_t)(0x0430 + (i % 26)); break;
        default:
            if (i & 1) { a[i] = (wchar_t)(0x0430 + (i % 26)); b[i] = (wchar_t)(0x0410 + (i % 26)); }
            else       { a[i] = (wchar_t)(L'a' + (i % 26));   b[i] = (wchar_t)(L'A' + (i % 26)); }
            break;
        }
    }
    if ((cls == 2 || cls == 5) && n > 0) b[n - 1] = (wchar_t)(a[n - 1] + 1);   /* differ at the end */
    a[n] = b[n] = 0;
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    static const int L[LENS] = { 64, 512, 4000 };
    enum { K = CLASSES * LENS };
    static CASE C[K];
    static wia_case cs[K];
    static char labels[K][28];
    int c, li, r = 0;

    sys = (FN)GetProcAddress(h, "CompareStringOrdinal");
    if (!sys) { printf("resolve failed\n"); return 1; }
    wia_upcase_init();
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== CompareStringOrdinal, IGNORE CASE, on strings that actually differ ==\n");
    printf("   change 210's non-ASCII row compares a string with ITSELF, which its own first tier\n");
    printf("   answers before the table is ever consulted\n\n");

    for (c = 0; c < CLASSES; ++c) {
        for (li = 0; li < LENS; ++li) {
            int n = L[li];
            wchar_t* a = (wchar_t*)malloc((size_t)(n + 2) * 2);
            wchar_t* b = (wchar_t*)malloc((size_t)(n + 2) * 2);
            build(c, a, b, n);
            C[r].a = a; C[r].b = b; C[r].n = n; C[r].ic = 1;
            sprintf(labels[r], "%s %d", CNAME[c], n);
            cs[r].label = labels[r]; cs[r].bytes = (size_t)n * 2;
            cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &C[r];
            ++r;
        }
    }

    /* what each class actually returns, printed so a row that compared nothing is visible */
    printf("   %-16s %8s %8s\n", "class", "ours", "live");
    for (c = 0; c < CLASSES; ++c) {
        int i = c * LENS + LENS - 1;
        printf("   %-16s %8d %8d\n", CNAME[c],
               wia_comparestringordinal(C[i].a, C[i].n, C[i].b, C[i].n, 1),
               sys(C[i].a, C[i].n, C[i].b, C[i].n, 1));
    }

    wia_bench_compare("CompareStringOrdinal ignore-case, by which tier the input reaches",
                      cs, r, 200);
    printf("\nThe `same` rows are tier one and reproduce the published figures. The `case` rows are\n"
           "the ones no published table has.\n");
    return 0;
}
