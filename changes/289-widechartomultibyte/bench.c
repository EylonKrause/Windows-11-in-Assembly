/* changes/289-widechartomultibyte/bench.c
 *
 * The size sweep is a size and class and mode sweep, for the reason change 016's RESULTS.md gives
 * at length: a table built only from the input a fast path was written for cannot say whether the
 * function is fast or whether the corpus was. That change published 2.61x on ASCII and was later
 * measured at 0.21x-0.94x on the input UTF-8 actually exists for.
 *
 * So every row here is one of:
 *
 *   ASCII    one byte per character, the 16-wide block
 *   2-byte   U+0080..U+07FF, the one-or-two-byte block
 *   Cyrillic U+0410.. , which is what discovery/desktop-startup-timings.md measured the shipped
 *            export at 1.071 ns/byte on: the worst row it found anywhere
 *   3-byte   U+0800..U+FFFF non-surrogate, the general BMP block
 *   pairs    surrogate pairs, the surrogate block
 *   mixed    ASCII alternating with two-byte, which is what European and Middle Eastern prose
 *            looks like once it has spaces and punctuation in it
 *   lone     lone surrogates, each becoming U+FFFD, no block handles these, so the row
 *            measures the scalar path on purpose
 *
 * and each class is run in all three MODES a real caller uses:
 *
 *   convert     an explicit cchWideChar and a generous destination
 *   measure     cbMultiByte = 0, the sizing half of the measure-then-convert idiom, the shipped
 *               export costs 476 ns for 4095 ASCII characters there, a quarter of the conversion
 *   cch = -1    a NUL-terminated source, which is how this function is most often called and which
 *               makes the length scan part of the measurement
 *
 * The overflow paths are a correctness question, and correctness.c asks it at every capacity from
 * zero upward.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "bench.h"

typedef int (WINAPI *F_WC2MB)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);
extern int wia_wc2mb(UINT, DWORD, const wchar_t*, int, char*, int, const char*, int*);
static F_WC2MB sys;

typedef struct { const wchar_t* src; int cch; char* dst; int cb; } ctx_t;

static uint64_t op_ours(void* c)
{ ctx_t* m = (ctx_t*)c; return (uint64_t)(unsigned)wia_wc2mb(65001, 0, m->src, m->cch, m->dst, m->cb, NULL, NULL); }
static uint64_t op_sys(void* c)
{ ctx_t* m = (ctx_t*)c; return (uint64_t)(unsigned)sys(65001, 0, m->src, m->cch, m->dst, m->cb, NULL, NULL); }

enum { CLASSES = 7, LENS = 4, MODES = 3 };
static const char* CNAME[CLASSES] = { "ASCII", "2-byte", "Cyril", "3-byte", "pairs", "mixed", "lone" };
static const char* MNAME[MODES]   = { "conv", "meas", "cch-1" };

static void fill(int c, wchar_t* s, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        switch (c) {
        case 0: s[i] = (wchar_t)('a' + (i & 15)); break;
        case 1: s[i] = (wchar_t)(0x00A0 + (i & 63)); break;
        case 2: s[i] = (wchar_t)(0x0410 + (i & 31)); break;      /* the discovery row */
        case 3: s[i] = (wchar_t)(0x20A0 + (i & 15)); break;
        case 4: if (i + 1 < n) { s[i] = (wchar_t)0xD83D; s[++i] = (wchar_t)(0xDE00 + (i & 15)); }
                else s[i] = L'a';
                break;
        case 5: s[i] = (i & 1) ? (wchar_t)0x00E9 : (wchar_t)('a' + (i & 15)); break;
        default: s[i] = (wchar_t)(0xD800 + (i & 0x3FF)); break;
        }
    }
    s[n] = 0;
}

int main(void)
{
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    static const int L[LENS] = { 64, 512, 4095, 32000 };
    enum { K = CLASSES * LENS * MODES };
    static ctx_t cx[K];
    static wia_case cs[K];
    static char labels[K][32];
    int c, li, mi, r = 0;

    sys = (F_WC2MB)GetProcAddress(kb, "WideCharToMultiByte");
    if (!sys) { printf("cannot resolve kernelbase!WideCharToMultiByte\n"); return 1; }

    for (c = 0; c < CLASSES; ++c) {
        for (li = 0; li < LENS; ++li) {
            int n = L[li];
            wchar_t* s = (wchar_t*)malloc((size_t)(n + 2) * 2);
            char* d = (char*)malloc((size_t)n * 4 + 64);
            fill(c, s, n);
            for (mi = 0; mi < MODES; ++mi) {
                cx[r].src = s;
                cx[r].dst = d;
                cx[r].cch = (mi == 2) ? -1 : n;
                cx[r].cb  = (mi == 1) ? 0 : (int)((size_t)n * 4 + 64);
                sprintf(labels[r], "%s %d %s", CNAME[c], n, MNAME[mi]);
                cs[r].label = labels[r];
                cs[r].bytes = (size_t)n * 2;
                cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &cx[r];
                ++r;
            }
        }
    }
    return wia_bench_compare(
        "WideCharToMultiByte CP_UTF8  (wia AVX2 vs live kernelbase -- every class, all three modes)",
        cs, r, 60);
}
