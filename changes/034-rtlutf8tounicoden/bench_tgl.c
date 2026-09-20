/* changes/034-rtlutf8tounicoden/bench_tgl.c
 *
 * The parent's bench.c cannot see what this variant exists to fix, so this file replaces it for the
 * Tiger Lake variant only. bench.c is NOT edited: it is the table `RESULTS.md` publishes, taken on
 * a machine with no AVX-512, and `build_tgl.bat` points here instead.
 *
 * What was wrong with the old table. Its six classes are ASCII, 2-byte, 3-byte, 4-byte, "mixed"
 * and u+fffd. Five of the six are homogeneous (every character the same width) and the sixth,
 * "mixed", is ASCII alternating with two-byte sequences, which is precisely the one mixture the
 * AVX2 file has a kernel for. So the table covers each width on its own, plus the one mixture that
 * has a kernel, and nothing else. `discovery/utf8_width_mixtures.c` measured the classes it cannot
 * express and got 0.29x to 0.52x against the shipped decoder, and change 290 is PARKED on the same
 * hole. A table that only contains the input a fast path was written for cannot say whether the
 * function is fast or whether the corpus was.
 *
 * So this table adds, and every one of them is a class the AVX2 blocks fall out of:
 *
 *   e0ed    three-byte sequences whose lead is 0xE0 or 0xED, Devanagari, Bengali, Tamil, Thai,
 *           and the top of the Hangul block. The AVX2 three-byte kernel DECLINES both leads by
 *           construction, because they are the two with a narrower second byte, so all of Indic
 *           and part of Korean used to go one character at a time.
 *   a+3     ASCII with three-byte sequences, English prose with a euro sign, and CJK with spaces
 *   a+4     ASCII with emoji
 *   2+3     two-byte with three-byte, Greek or Hebrew with punctuation above U+2000
 *   1234    all four widths interleaved
 *   bad32   ASCII with one malformed byte every 32, a log file, a network buffer, user input
 *   rand    bytes drawn the way correctness.c's fuzz draws them: mostly malformed, every class
 *
 * ALIGNMENT. Change 294's lesson is that the (length, alignment) pair malloc happens to hand out
 * is not the speed gate, and change 296's is that (dst - src) mod 4096 can cost a size class on its
 * own. Both axes are swept in probes/tglaxes.c. This table then runs every row at the WORST
 * alignment a 64-byte-load kernel can have, source at offset 63 of a 4K page, so every wide load
 * straddles a cache line and the tail falls at the least convenient place, with the destination
 * off its own 64-byte boundary too. The numbers below are therefore a floor, not a best case.
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

enum { CLASSES = 13, LENS = 4 };
static const char* CNAME[CLASSES] = {
    "ascii", "2byte", "3byte", "4byte", "fffd", "e0ed",
    "a+2", "a+3", "a+4", "2+3", "1234", "bad32", "rand"
};

/* n BYTES of input in the given class. Every sequence produced is well formed except in the two
   classes whose whole point is that they are not. */
static void fill(int c, unsigned char* s, int n)
{
    int i = 0, k = 0;
    unsigned long seed = 12345u;
    while (i < n) {
        switch (c) {
        case 0:  s[i++] = (unsigned char)('a' + (i & 15)); break;
        case 1:  if (i + 1 < n) { s[i++] = (unsigned char)(0xC2 + (i & 15));
                                  s[i++] = (unsigned char)(0x80 + (i & 63)); } else s[i++] = 'z';
                 break;
        case 2:  if (i + 2 < n) { s[i++] = (unsigned char)(0xE1 + (i & 7));
                                  s[i++] = (unsigned char)(0x80 + (i & 63));
                                  s[i++] = (unsigned char)(0x80 + (i & 63)); } else s[i++] = 'z';
                 break;
        case 3:  if (i + 3 < n) { int lead = k++ & 3;
                                  s[i++] = (unsigned char)(0xF0 + lead);
                                  s[i++] = (unsigned char)((lead ? 0x80 : 0x90) + (i & 15));
                                  s[i++] = (unsigned char)(0x80 + (i & 63));
                                  s[i++] = (unsigned char)(0x80 + (i & 63)); } else s[i++] = 'z';
                 break;
        case 4:  if (i + 2 < n) { s[i++] = 0xEF; s[i++] = 0xBF; s[i++] = 0xBD; } else s[i++] = 'z';
                 break;
        /* 0xE0 needs a second byte of 0xA0..0xBF and 0xED one of 0x80..0x9F, the two leads the
           AVX2 three-byte kernel refuses. U+0900.. is Devanagari; U+D7xx is the end of Hangul. */
        case 5:  if (i + 2 < n) { if (k++ & 1) { s[i++] = 0xE0; s[i++] = (unsigned char)(0xA4 + (i & 7)); }
                                  else        { s[i++] = 0xED; s[i++] = (unsigned char)(0x80 + (i & 15)); }
                                  s[i++] = (unsigned char)(0x80 + (i & 63)); } else s[i++] = 'z';
                 break;
        case 6:  if (i + 2 < n) { s[i++] = (unsigned char)('a' + (i & 15));
                                  s[i++] = 0xC3; s[i++] = 0xA9; } else s[i++] = 'z';
                 break;
        case 7:  if (i + 3 < n) { s[i++] = (unsigned char)('a' + (i & 15));
                                  s[i++] = 0xE2; s[i++] = 0x82; s[i++] = 0xAC; } else s[i++] = 'z';
                 break;
        case 8:  if (i + 4 < n) { s[i++] = (unsigned char)('a' + (i & 15));
                                  s[i++] = 0xF0; s[i++] = 0x9F; s[i++] = 0x98; s[i++] = 0x80; }
                 else s[i++] = 'z';
                 break;
        case 9:  if (i + 4 < n) { s[i++] = 0xC3; s[i++] = 0xA9;
                                  s[i++] = 0xE2; s[i++] = 0x82; s[i++] = 0xAC; } else s[i++] = 'z';
                 break;
        case 10: if (i + 9 < n) { s[i++] = (unsigned char)('a' + (i & 15));
                                  s[i++] = 0xC3; s[i++] = 0xA9;
                                  s[i++] = 0xE2; s[i++] = 0x82; s[i++] = 0xAC;
                                  s[i++] = 0xF0; s[i++] = 0x9F; s[i++] = 0x98; s[i++] = 0x80; }
                 else s[i++] = 'z';
                 break;
        case 11: s[i] = ((i % 32) == 31) ? 0x80 : (unsigned char)('a' + (i & 15)); ++i; break;
        default: { unsigned r; int p;
                   seed = seed * 1103515245u + 12345u; r = (unsigned)(seed >> 8); p = r % 10;
                   if (p < 5)      s[i] = (unsigned char)(r % 0x80);
                   else if (p < 7) s[i] = (unsigned char)(0xC0 + (r % 0x40));
                   else if (p < 8) s[i] = (unsigned char)(0xE0 + (r % 0x10));
                   else if (p < 9) s[i] = (unsigned char)(0xF0 + (r % 8));
                   else            s[i] = (unsigned char)(0x80 + (r % 0x40));
                   ++i; }
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
            /* The worst alignment a 64-BYTE-LOAD kernel can have, on purpose: the source starts at
               offset 63 of a 4K-aligned block, so every wide load straddles a cache line; the
               destination starts 2 bytes off its own 64-byte boundary. See probes/tglaxes.c. */
            unsigned char* sb = (unsigned char*)_aligned_malloc((size_t)n + 256, 4096);
            unsigned char* db = (unsigned char*)_aligned_malloc(((size_t)n + 256) * 2, 4096);
            unsigned char* s = sb + 63;
            fill(c, s, n);
            cx[r].src = s; cx[r].sb = (ULONG)n;
            cx[r].dst = (wchar_t*)(db + 2);
            cx[r].db = (ULONG)(((size_t)n + 8) * 2);
            sprintf(labels[r], "%s %d", CNAME[c], n);
            cs[r].label = labels[r]; cs[r].bytes = (size_t)n;
            cs[r].ours = op_ours; cs[r].system = op_sys; cs[r].ctx = &cx[r];
            ++r;
        }
    }
    return wia_bench_compare(
        "RtlUTF8ToUnicodeN  (impl_tgl AVX-512 VBMI2 vs live ntdll -- MIXED widths and malformed, "
        "worst alignment)", cs, r, 100);
}
