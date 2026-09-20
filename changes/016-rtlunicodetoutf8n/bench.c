/* changes/016-rtlunicodetoutf8n/bench.c
 *
 * This table used to be ASCII only, and for a UTF-8 encoder that was the wrong table to publish.
 *
 * Every row was ASCII input, which is the one case the original fast path handled, so the 2.61x it
 * reported was the speed of a path a caller converting Hebrew, Greek, Cyrillic, CJK or emoji never
 * reaches. discovery/utf8_nonascii_rows.c asked the same function about the input UTF-8 exists for
 * and got 0.21x to 0.94x. A table that only contains the input a fast path was written for cannot
 * say whether the function is fast or whether the corpus was.
 *
 * So the size sweep is now a size AND CLASS sweep: every length, in each of the six classes the
 * implementation actually distinguishes.
 *
 *   ASCII        one byte per character, the 16-wide block
 *   2-byte       U+0080..U+07FF, the one-or-two-byte block
 *   3-byte       U+0800..U+FFFF non-surrogate, the general BMP block
 *   pairs        surrogate pairs, four bytes per pair, the surrogate block
 *   mixed        ASCII alternating with two-byte, what European and Middle Eastern prose looks
 *                like once it has spaces and punctuation in it, and the commonest non-ASCII input
 *                there is
 *   lone         lone surrogates, each becoming U+FFFD with STATUS_SOME_NOT_MAPPED: the class no
 *                block handles, so it measures the scalar path on purpose
 *
 * The destination is generous on every row. The overflow paths are a correctness question, and
 * correctness.c asks it at every capacity from 0 upward.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "bench.h"
typedef LONG NTSTATUS;
extern NTSTATUS wia_u2u8(void*, ULONG, PULONG, const wchar_t*, ULONG);
typedef NTSTATUS (WINAPI *fn)(void*, ULONG, PULONG, const wchar_t*, ULONG);
static fn sys;
typedef struct { const wchar_t* src; ULONG srcBytes; unsigned char* dst; ULONG dstMax; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; ULONG o=0; wia_u2u8(m->dst,m->dstMax,&o,m->src,m->srcBytes); return o; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; ULONG o=0; sys(m->dst,m->dstMax,&o,m->src,m->srcBytes); return o; }

enum { CLASSES = 6, LENS = 4 };
static const char* CNAME[CLASSES] = { "ASCII", "2-byte", "3-byte", "pairs", "mixed", "lone" };

static void fill(int c, wchar_t* s, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        switch (c) {
        case 0: s[i] = (wchar_t)('a' + (i & 15)); break;
        case 1: s[i] = (wchar_t)(0x00A0 + (i & 63)); break;
        case 2: s[i] = (wchar_t)(0x20A0 + (i & 15)); break;
        case 3: if (i + 1 < n) { s[i] = (wchar_t)0xD83D; s[++i] = (wchar_t)(0xDE00 + (i & 15)); }
                else s[i] = L'a';
                break;
        case 4: s[i] = (i & 1) ? (wchar_t)0x00E9 : (wchar_t)('a' + (i & 15)); break;
        default: s[i] = (wchar_t)(0xD800 + (i & 0x3FF)); break;
        }
    }
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    static const int L[LENS] = { 64, 512, 4000, 32000 };
    enum { K = CLASSES * LENS };
    static ctx_t cx[K];
    static wia_case cs[K];
    static char labels[K][24];
    int c, li, r = 0;
    sys = (fn)GetProcAddress(h, "RtlUnicodeToUTF8N");
    for (c = 0; c < CLASSES; ++c) {
        for (li = 0; li < LENS; ++li) {
            int n = L[li];
            wchar_t* s = (wchar_t*)malloc((size_t)(n + 2) * 2);
            fill(c, s, n);
            cx[r].src = s; cx[r].srcBytes = (ULONG)(n * 2);
            cx[r].dst = (unsigned char*)malloc((size_t)n * 4 + 8);
            cx[r].dstMax = (ULONG)((size_t)n * 4 + 8);
            sprintf(labels[r], "%s %d", CNAME[c], n);
            cs[r].label = labels[r]; cs[r].bytes = (size_t)n * 2;
            cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &cx[r];
            ++r;
        }
    }
    return wia_bench_compare(
        "RtlUnicodeToUTF8N  (wia AVX2 vs ntdll, every input class -- not just ASCII)", cs, r, 100);
}
