/* changes/027-rtlupcaseunicodetomultibyten/bench.c
 *
 * This table used to be `L'a' + (k & 15)` and nothing else, which for an upcase conversion with a
 * TABLE PATH is the wrong table to publish. ASCII is the one case the vector block handles; every
 * character above 0x7F goes through the 65536-entry map, and that path had never been timed.
 *
 * discovery/upcase_nonascii_rows.c asked, and the answer was 0.59x against the shipped export on
 * Cyrillic or CJK, in a change published at 10.24x. The cause was not the table: it was the
 * scalar walk re-entering the vector loop once per character, which is change 263's rule, and the
 * sibling that does the same job with the same table (change 020) already had it right.
 *
 * So the size sweep is now a size AND CLASS sweep, in the classes the implementation distinguishes:
 *
 *   ascii-lower   the published row, and the one the vector block exists for
 *   ascii-mixed   the same path with different letters actually changing
 *   latin-1       the table path, where the fold DOES something
 *   cyrillic      the table path on text the ANSI/OEM code page cannot represent
 *   cjk           the table path where the fold changes nothing at all, the lookup, with
 *                 nothing to show for it, which is the worst case this function has
 *   mixed         ASCII alternating with a two-byte letter, which is what real prose looks like
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "bench.h"
typedef LONG NTSTATUS;
extern NTSTATUS wia_u2umb(char*, ULONG, PULONG, const wchar_t*, ULONG);
void wia_upansimap_init(void);
typedef NTSTATUS (WINAPI *fn_t)(char*, ULONG, PULONG, const wchar_t*, ULONG);
static fn_t sys;
typedef struct { const wchar_t* src; ULONG sb; char* dst; ULONG mx; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; ULONG o=0; wia_u2umb(m->dst,m->mx,&o,m->src,m->sb); return o; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; ULONG o=0; sys(m->dst,m->mx,&o,m->src,m->sb); return o; }

enum { CLASSES = 6, LENS = 4 };
static const char* CNAME[CLASSES] = { "ascii-low", "ascii-mix", "latin-1", "cyrillic", "cjk", "mixed" };

static void fill(int c, wchar_t* s, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        switch (c) {
        case 0: s[i] = (wchar_t)(L'a' + (i % 26)); break;
        case 1: s[i] = (wchar_t)((i & 1) ? (L'a' + (i % 26)) : (L'A' + (i % 26))); break;
        case 2: s[i] = (wchar_t)(0x00E0 + (i % 0x18)); break;
        case 3: s[i] = (wchar_t)(0x0430 + (i % 26)); break;
        case 4: s[i] = (wchar_t)(0x4E00 + (i % 512)); break;
        default: s[i] = (wchar_t)((i & 1) ? 0x00E9 : (L'a' + (i % 26))); break;
        }
    }
    s[n] = 0;
}

int main(void){
    static const int L[LENS] = { 64, 512, 4000, 32000 };
    enum { K = CLASSES * LENS };
    static ctx_t cx[K];
    static wia_case cs[K];
    static char labels[K][24];
    HMODULE h;
    int c, li, r = 0;
    wia_upansimap_init();
    h = LoadLibraryW(L"ntdll.dll"); sys = (fn_t)GetProcAddress(h, "RtlUpcaseUnicodeToMultiByteN");
    for (c = 0; c < CLASSES; ++c) {
        for (li = 0; li < LENS; ++li) {
            int n = L[li];
            wchar_t* s = (wchar_t*)malloc((size_t)(n + 2) * 2);
            fill(c, s, n);
            cx[r].src = s; cx[r].sb = (ULONG)(n * 2);
            cx[r].dst = (char*)malloc((size_t)n + 8); cx[r].mx = (ULONG)(n + 8);
            sprintf(labels[r], "%s %d", CNAME[c], n);
            cs[r].label = labels[r]; cs[r].bytes = (size_t)n * 2;
            cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &cx[r];
            ++r;
        }
    }
    return wia_bench_compare(
        "RtlUpcaseUnicodeToMultiByteN  (wia AVX2 vs ntdll, every input class -- not just ASCII)", cs, r, 100);
}
