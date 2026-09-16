/* changes/034-rtlutf8tounicoden/bench.c
 *
 * THIS TABLE USED TO BE ASCII ONLY, and for a UTF-8 decoder that was the wrong table to publish.
 *
 * Every row was ASCII input, which is the one case the original fast path handled, so the 3.61x it
 * reported was the speed of a path a caller decoding Hebrew, Greek, Cyrillic, CJK or emoji never
 * reaches. discovery/utf8_nonascii_rows.c asked the same function about the input UTF-8 exists for
 * and got 0.33x to 0.48x. A table that only contains the input a fast path was written for cannot
 * say whether the function is fast or whether the corpus was.
 *
 * So the size sweep is now a size AND CLASS sweep, in the classes the implementation actually
 * distinguishes:
 *
 *   ASCII     one byte per unit, the 16-wide block
 *   2-byte    C2..DF 80..BF, the two-byte block: sixteen bytes, eight units, no shuffle
 *   3-byte    E1..EF 80..BF 80..BF, the three-byte block: twenty-four bytes, eight units
 *   4-byte    F0..F3, the four-byte block: sixteen bytes, four surrogate PAIRS
 *   mixed     ASCII alternating with two-byte -- what European and Middle Eastern prose looks like
 *             once it has spaces and punctuation in it, and the commonest non-ASCII input there is
 *   U+FFFD    EF BF BD repeated: the three-byte block again, and also what this decoder's own
 *             substitution of malformed input produces, so it is the shape of re-decoded text
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
extern NTSTATUS wia_u82u(wchar_t*, ULONG, PULONG, const void*, ULONG);
typedef NTSTATUS (WINAPI *fn)(wchar_t*, ULONG, PULONG, const void*, ULONG);
static fn sys;
typedef struct { const void* src; ULONG sb; wchar_t* dst; ULONG db; } ctx_t;
static uint64_t op_ours(void*c){ ctx_t*m=(ctx_t*)c; ULONG o=0; wia_u82u(m->dst,m->db,&o,m->src,m->sb); return o; }
static uint64_t op_sys (void*c){ ctx_t*m=(ctx_t*)c; ULONG o=0; sys(m->dst,m->db,&o,m->src,m->sb); return o; }

enum { CLASSES = 6, LENS = 4 };
static const char* CNAME[CLASSES] = { "ASCII", "2-byte", "3-byte", "4-byte", "mixed", "U+FFFD" };

/* n BYTES of input in the given class */
static void fill(int c, unsigned char* s, int n)
{
    int i = 0;
    while (i < n) {
        switch (c) {
        case 0: s[i++] = (unsigned char)('a' + (i & 15)); break;
        case 1: if (i + 1 < n) { s[i++] = (unsigned char)(0xC2 + (i & 15)); s[i++] = (unsigned char)(0x80 + (i & 63)); }
                else s[i++] = 'z';
                break;
        case 2: if (i + 2 < n) { s[i++] = (unsigned char)(0xE1 + (i & 7)); s[i++] = (unsigned char)(0x80 + (i & 63));
                                 s[i++] = (unsigned char)(0x80 + (i & 63)); }
                else s[i++] = 'z';
                break;
        case 3: if (i + 3 < n) { s[i++] = 0xF0; s[i++] = 0x9F; s[i++] = 0x98; s[i++] = (unsigned char)(0x80 + (i & 15)); }
                else s[i++] = 'z';
                break;
        case 4: if ((i & 1) == 0) s[i++] = (unsigned char)('a' + (i & 15));
                else if (i + 1 < n) { s[i++] = 0xC3; s[i++] = 0xA9; }
                else s[i++] = 'z';
                break;
        default: if (i + 2 < n) { s[i++] = 0xEF; s[i++] = 0xBF; s[i++] = 0xBD; }
                 else s[i++] = 'z';
                 break;
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
    sys = (fn)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    for (c = 0; c < CLASSES; ++c) {
        for (li = 0; li < LENS; ++li) {
            int n = L[li];
            unsigned char* s = (unsigned char*)malloc((size_t)n + 8);
            fill(c, s, n);
            cx[r].src = s; cx[r].sb = (ULONG)n;
            cx[r].dst = (wchar_t*)malloc(((size_t)n + 8) * 2);
            cx[r].db = (ULONG)(((size_t)n + 8) * 2);
            sprintf(labels[r], "%s %d", CNAME[c], n);
            cs[r].label = labels[r]; cs[r].bytes = (size_t)n;
            cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &cx[r];
            ++r;
        }
    }
    return wia_bench_compare(
        "RtlUTF8ToUnicodeN  (wia AVX2 vs ntdll, every input class -- not just ASCII)", cs, r, 100);
}
