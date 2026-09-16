/* changes/265-rtlappendasciiztostring/bench.c
 *
 * OURS vs the LIVE ntdll!RtlAppendAsciizToString.
 *
 * THE WORK IS A SCAN AND A COPY, and the contract forces BOTH: nothing may be written until the
 * length is known, because a source that does not fit must leave the buffer untouched. So the
 * source is read twice by construction and the subject of every row is its length.
 *
 * THE DESTINATION IS RESET TO EMPTY INSIDE THE OP, which sounds like the mistake change 142 made --
 * its bench undid an in-place edit with a memcpy of the whole path and the restore REPLACED the
 * measurement. It is not the same thing here: the reset is TWO STORES to the STRING header
 * (Length = 0), not a copy of the buffer, and it is identical on both sides. Without it the
 * destination fills up and every call after the first few is a refusal -- the row would silently
 * stop measuring the copy at all.
 *
 * THE REFUSAL PATH GETS ITS OWN ROWS, because it is the cheap answer this function is expected to
 * give quickly and it is where an implementation that scanned before checking would show up.
 *
 * THE SHORT ROWS CALL SIXTEEN TIMES PER TIMED OP, for the reason change 261 established by
 * measuring it: an empty call through this harness costs 2.32 ns, which is most of what an 8-byte
 * append measures. Their labels say so.
 */
#include "bench.h"

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } ASTR;
typedef LONG (NTAPI *F_App)(ASTR*, const char*);

LONG wia_appendasciiztostring(void*, const char*);
static F_App live;

typedef struct { ASTR d; const char* src; ULONG reps; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) {
        c->d.Length = 0;                       /* two stores, not a buffer copy: see the note */
        acc += (unsigned)wia_appendasciiztostring(&c->d, c->src);
    }
    return acc;
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) {
        c->d.Length = 0;
        acc += (unsigned)live(&c->d, c->src);
    }
    return acc;
}

#define MAXCASE 16
#define CAP     9000
static char     srcpool[MAXCASE][CAP];
static char     dstpool[MAXCASE][CAP];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static void add(const char* label, size_t len, int fits)
{
    int k = nc;
    size_t i;
    for (i = 0; i < len; ++i) srcpool[k][i] = (char)('a' + (i % 26));
    srcpool[k][len] = 0;
    ctxs[k].src = srcpool[k];
    ctxs[k].d.Length = 0;
    ctxs[k].d.MaximumLength = fits ? (USHORT)(len + 8) : (USHORT)(len ? len - 1 : 0);
    ctxs[k].d.Buffer = dstpool[k];
    ctxs[k].reps = (len <= 64) ? 16 : 1;
    cases[k].label  = label;
    cases[k].bytes  = (fits ? len : 0) * ctxs[k].reps;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live = (F_App)GetProcAddress(h, "RtlAppendAsciizToString");
    if (!live) { printf("RtlAppendAsciizToString not found\n"); return 1; }

    /*   label                                     len   fits */
    add("append 4000 bytes (the survey subject)", 4000, 1);
    add("append 8000 bytes",                      8000, 1);
    add("append 1000 bytes",                      1000, 1);
    add("append 400 bytes",                        400, 1);
    add("append 100 bytes",                        100, 1);
    add("append 64 bytes (x16 calls)",              64, 1);
    add("append 32 bytes (x16 calls)",              32, 1);
    add("append 16 bytes (x16 calls)",              16, 1);
    add("append 8 bytes (x16 calls)",                8, 1);
    add("append 1 byte (x16 calls)",                 1, 1);
    add("append the empty string (x16 calls)",       0, 1);
    add("REFUSED: 4000 bytes, one too few",       4000, 0);
    add("REFUSED: 100 bytes, one too few",         100, 0);
    add("REFUSED: 8 bytes, one too few (x16)",       8, 0);

    printf("== SUBJECTS (what each row actually does) ==\n");
    printf("  %-44s %6s %6s   %-18s %s\n", "case", "srclen", "max", "ours", "live");
    for (i = 0; i < nc; ++i) {
        LONG ro, rl;
        USHORT lo, ll;
        ctxs[i].d.Length = 0;
        ro = wia_appendasciiztostring(&ctxs[i].d, ctxs[i].src);
        lo = ctxs[i].d.Length;
        ctxs[i].d.Length = 0;
        rl = live(&ctxs[i].d, ctxs[i].src);
        ll = ctxs[i].d.Length;
        ctxs[i].d.Length = 0;
        printf("  %-44s %6Iu %6u   %08lX len=%-5u  %08lX len=%-5u%s\n", cases[i].label,
               strlen(ctxs[i].src), ctxs[i].d.MaximumLength,
               (unsigned long)ro, lo, (unsigned long)rl, ll,
               (ro == rl && lo == ll) ? "" : "   <== DISAGREE");
        if (ro != rl || lo != ll) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlAppendAsciizToString", cases, nc, 25);
}
