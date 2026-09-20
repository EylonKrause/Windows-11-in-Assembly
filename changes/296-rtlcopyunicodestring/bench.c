/* changes/296-rtlcopyunicodestring/bench.c -- wia_copyus vs the LIVE ntdll export, by size class.
 *
 * Two tables, and the change lands only if neither reports a regression:
 *
 *   [1] the ordinary call: the whole source fits, so n = src->Length and a wide NUL is written.
 *       This is the shape every one of the 31 live modules that bind this export makes, and the
 *       one discovery/ntdll_tier3.c timed (3.55 ns at 8 wchars, 12.50 at 254, 113.30 at 4095 --
 *       about 40 GB/s at 254 characters).
 *
 *   [2] the TRUNCATING call, with dst->MaximumLength ODD and smaller than src->Length. That path
 *       copies MaximumLength bytes exactly -- an odd byte count -- and writes no NUL, so it lands
 *       on ladder arms case [1] never touches. A size class that only exists on a contract edge
 *       still has to not regress.
 *
 * WHY THE DESTINATION OFFSET ROTATES, which is the one thing this bench does differently from its
 * neighbours in this tree. A wide copy's speed depends on the alignment of the destination and on
 * (dst - src) mod 32, for ntdll as much as for us: probes/detail.c measures the live export itself
 * ranging from 21.2 ns to 57.6 ns at one single length, purely by moving the destination. With one
 * malloc'd buffer per class, a size class's verdict is therefore decided by where that executable's
 * heap happened to land -- and it is not even stable across rebuilds, because relinking moves the
 * heap. Six builds of this change measured the 128-wchar class, running identical code in five of
 * them, at 5.00, 6.14, 6.30, 6.34, 6.66 and 7.41 ns.
 *
 * So each case walks its destination through eight offsets spanning every 8-byte alignment class
 * within a 64-byte line. Both implementations are handed exactly the same sequence of addresses,
 * so the comparison stays fair, and what the table reports is the average caller rather than one
 * lucky one. The rotation costs an increment and a mask, paid identically by both sides.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern void wia_copyus(US*, const US*);
typedef void (NTAPI *fn)(US*, const US*);
static fn sys;

/* Eight destination offsets, in bytes, all WCHAR-aligned (a PWSTR never is not). They cover
   dst % 32 in {0,2,8,16,24} and both halves of a 64-byte line. */
static const int OFF[8] = { 0, 2, 8, 16, 24, 32, 40, 48 };

typedef struct { unsigned char* dbase; USHORT maxlen; US src; unsigned i; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    US u = { 0, m->maxlen, (wchar_t*)(m->dbase + OFF[m->i++ & 7]) };
    wia_copyus(&u, &m->src);
    return u.Length;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    US u = { 0, m->maxlen, (wchar_t*)(m->dbase + OFF[m->i++ & 7]) };
    sys(&u, &m->src);
    return u.Length;
}

enum { K = 11 };
static const int         L[K] = { 1, 2, 4, 8, 16, 32, 64, 128, 254, 1024, 4095 };
static const char* const N[K] = { "1w", "2w", "4w", "8w", "16w", "32w", "64w", "128w", "254w", "1024w", "4095w" };

static ctx_t cx[K], cy[K];
static wia_case cs[K], ct[K];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    sys = (fn)GetProcAddress(h, "RtlCopyUnicodeString");
    if (!sys) { printf("no ntdll!RtlCopyUnicodeString\n"); return 2; }

    /* Page-aligned arenas, so the addresses are declared rather than inherited from the heap. */
    for (int i = 0; i < K; ++i) {
        int n = L[i];
        size_t span = (size_t)(n + 1) * 2 + 4096;
        wchar_t* s = (wchar_t*)VirtualAlloc(NULL, span, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        for (int k = 0; k < n; ++k) s[k] = (wchar_t)(L'a' + (k % 26));
        s[n] = 0;

        /* [1] the source fits; a NUL is written */
        cx[i].dbase  = (unsigned char*)VirtualAlloc(NULL, span + 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        cx[i].maxlen = (USHORT)(n * 2 + 4);
        cx[i].i = 0;
        cx[i].src.Length = (USHORT)(n * 2); cx[i].src.MaximumLength = (USHORT)(n * 2); cx[i].src.Buffer = s;
        cs[i].label = N[i]; cs[i].bytes = (size_t)n * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];

        /* [2] truncates, ODD MaximumLength, no NUL */
        cy[i].dbase  = (unsigned char*)VirtualAlloc(NULL, span + 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        cy[i].maxlen = (USHORT)(n * 2 - 1);            /* odd, and short by one byte */
        if (n == 1) cy[i].maxlen = 1;
        cy[i].i = 0;
        cy[i].src.Length = (USHORT)(n * 2); cy[i].src.MaximumLength = (USHORT)(n * 2); cy[i].src.Buffer = s;
        ct[i].label = N[i]; ct[i].bytes = (size_t)cy[i].maxlen;
        ct[i].ours = op_ours; ct[i].system = op_sys; ct[i].ctx = &cy[i];
    }

    int bad = 0;
    bad |= wia_bench_compare("RtlCopyUnicodeString [1] fits, NUL written (wia frameless inline AVX2/ERMS vs ntdll frame+memmove call)", cs, K, 200);
    bad |= wia_bench_compare("RtlCopyUnicodeString [2] TRUNCATING, ODD MaximumLength, no NUL", ct, K, 200);
    return bad;
}
