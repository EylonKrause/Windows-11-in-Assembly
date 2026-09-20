/* changes/290-multibytetowidechar/bench.c
 *
 * The table is a size *and class* sweep, and six of the ten classes are mixed-width on purpose.
 *
 * discovery/utf8_width_mixtures.c is the reason.  Change 034 -- the same transformation one layer
 * down -- published 3.96x over six classes, five of which are HOMOGENEOUS (every character the
 * same width) and the sixth of which was "ASCII alternating with two-byte", the one mixture it had
 * a kernel for.  Asked about eight mixtures it had no kernel for, the same assembly ran 0.34x to
 * 0.63x.  A table that only contains the input a fast path was written for cannot say whether the
 * function is fast or whether the corpus was.
 *
 * So the classes here are:
 *
 *   ASCII, 2-byte, 3-byte, 4-byte       the runs -- Latin, Greek/Cyrillic/Hebrew, CJK, emoji
 *   ASCII+2, ASCII+3, ASCII+4           prose: text with accents, with CJK, with emoji
 *   2+3, 1+2+3+4                        the genuinely heterogeneous cases
 *   malformed                           random bytes, which is what a server gets fed
 *
 * and three MODES, because MultiByteToWideChar has three and a caller uses two of them per string:
 *
 *   convert     a generous destination, the case every table publishes
 *   measure     cchWideChar == 0, the first half of the standard two-call idiom
 *   NUL-term    cbMultiByte == -1, where the shipped code runs a byte-at-a-time strlen
 *
 * The destination is generous on every converting row.  The overflow paths are a correctness
 * question and correctness.c asks it at every capacity from 0 upward.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "bench.h"

extern int  wia_mbtwc(UINT, DWORD, const char*, int, wchar_t*, int);
extern void wia_mbtwc_set_fallback(void*);

typedef int (WINAPI *FN)(UINT, DWORD, LPCCH, int, LPWSTR, int);
static FN sys;

typedef struct { const char* src; int cb; wchar_t* dst; int cch; } ctx_t;

static uint64_t op_ours(void* c)
{ ctx_t* m = (ctx_t*)c; return (uint64_t)(unsigned)wia_mbtwc(CP_UTF8, 0, m->src, m->cb, m->dst, m->cch); }
static uint64_t op_sys(void* c)
{ ctx_t* m = (ctx_t*)c; return (uint64_t)(unsigned)sys(CP_UTF8, 0, m->src, m->cb, m->dst, m->cch); }

enum { CLASSES = 10 };
static const char* CNAME[CLASSES] = {
    "ascii", "2byte", "3byte", "4byte", "a+2", "a+3", "a+4", "2+3", "1234", "bad"
};

static void put2(unsigned char* s, int* pi, int n, int t)
{ if (*pi + 1 < n) { s[(*pi)++] = (unsigned char)(0xC2 + (t % 0x1E)); s[(*pi)++] = (unsigned char)(0x80 + (t % 0x40)); } else s[(*pi)++] = 'z'; }
static void put3(unsigned char* s, int* pi, int n, int t)
{ if (*pi + 2 < n) { s[(*pi)++] = (unsigned char)(0xE4 + (t % 0x08)); s[(*pi)++] = (unsigned char)(0x80 + (t % 0x40)); s[(*pi)++] = (unsigned char)(0x80 + ((t >> 2) % 0x40)); } else s[(*pi)++] = 'z'; }
static void put4(unsigned char* s, int* pi, int n, int t)
{
    int lead = t % 4;
    if (*pi + 3 < n) {
        s[(*pi)++] = (unsigned char)(0xF0 + lead);
        s[(*pi)++] = (unsigned char)((lead ? 0x80 : 0x90) + (t % 0x30));
        s[(*pi)++] = (unsigned char)(0x80 + (t % 0x40));
        s[(*pi)++] = (unsigned char)(0x80 + ((t >> 3) % 0x40));
    } else s[(*pi)++] = 'z';
}

static void fill(int c, unsigned char* s, int n)
{
    int i = 0, t = 0;
    unsigned st = 0x1337u;
    while (i < n) {
        switch (c) {
        case 0: s[i] = (unsigned char)('a' + (i % 26)); ++i; break;
        case 1: put2(s, &i, n, t); break;
        case 2: put3(s, &i, n, t); break;
        case 3: put4(s, &i, n, t); break;
        case 4: if ((t & 1) == 0) s[i++] = (unsigned char)('a' + (i % 26)); else put2(s, &i, n, t); break;
        case 5: if ((t & 1) == 0) s[i++] = (unsigned char)('a' + (i % 26)); else put3(s, &i, n, t); break;
        case 6: if ((t & 1) == 0) s[i++] = (unsigned char)('a' + (i % 26)); else put4(s, &i, n, t); break;
        case 7: if ((t & 1) == 0) put2(s, &i, n, t); else put3(s, &i, n, t); break;
        case 8: switch (t & 3) {
                case 0: s[i++] = (unsigned char)('a' + (i % 26)); break;
                case 1: put2(s, &i, n, t); break;
                case 2: put3(s, &i, n, t); break;
                default: put4(s, &i, n, t); break; } break;
        default: st = st * 1103515245u + 12345u; s[i] = (unsigned char)(st >> 17); ++i; break;
        }
        ++t;
    }
}

int main(void)
{
    static const int L[] = { 8, 64, 512, 4096, 8191, 32000 };
    enum { LENS = 6, KMAX = CLASSES * LENS + CLASSES * 2 + 3 };
    static ctx_t cx[KMAX];
    static wia_case cs[KMAX];
    static char labels[KMAX][24];
    int c, li, r = 0;

    sys = (FN)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "MultiByteToWideChar");
    wia_mbtwc_set_fallback((void*)sys);

    /* --- converting, a generous destination --- */
    for (c = 0; c < CLASSES; ++c)
        for (li = 0; li < LENS; ++li) {
            int n = L[li];
            unsigned char* s = (unsigned char*)malloc((size_t)n + 8);
            fill(c, s, n);
            cx[r].src = (const char*)s; cx[r].cb = n;
            cx[r].dst = (wchar_t*)malloc(((size_t)n + 8) * 2);
            cx[r].cch = n + 4;
            sprintf(labels[r], "%s %d", CNAME[c], n);
            cs[r].label = labels[r]; cs[r].bytes = (size_t)n;
            cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &cx[r];
            ++r;
        }

    /* --- the measuring mode, which a caller runs on every string before converting it --- */
    for (c = 0; c < CLASSES; ++c) {
        int k;
        for (k = 0; k < 2; ++k) {
            int n = k ? 8191 : 512;
            unsigned char* s = (unsigned char*)malloc((size_t)n + 8);
            fill(c, s, n);
            cx[r].src = (const char*)s; cx[r].cb = n; cx[r].dst = NULL; cx[r].cch = 0;
            sprintf(labels[r], "m:%s %d", CNAME[c], n);
            cs[r].label = labels[r]; cs[r].bytes = (size_t)n;
            cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &cx[r];
            ++r;
        }
    }

    /* --- cbMultiByte == -1, where the shipped code walks the string one byte at a time --- */
    for (li = 0; li < 3; ++li) {
        int n = L[li + 2];
        unsigned char* s = (unsigned char*)malloc((size_t)n + 8);
        fill(0, s, n); s[n] = 0;
        cx[r].src = (const char*)s; cx[r].cb = -1;
        cx[r].dst = (wchar_t*)malloc(((size_t)n + 8) * 2);
        cx[r].cch = n + 4;
        sprintf(labels[r], "z:ascii %d", n);
        cs[r].label = labels[r]; cs[r].bytes = (size_t)n;
        cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &cx[r];
        ++r;
    }

    return wia_bench_compare(
        "MultiByteToWideChar CP_UTF8 (wia AVX2 vs kernel32) -- ten classes, six of them mixed-width",
        cs, r, 60);
}
