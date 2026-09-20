/* changes/267-rtlcrc32/bench.c
 *
 * OURS vs the LIVE ntdll!RtlCrc32.
 *
 * The work is proportional to the length and there is no early exit, every byte enters the crc --
 * so the subject of every row is simply its size. What the rows are chosen AROUND is the two block
 * boundaries: 192 bytes, where the short three-way split begins, and 3072, where the long one does.
 * A row set that sampled only round numbers would miss the sizes where a split has just become
 * possible or just stopped being possible, which is exactly where a three-way implementation can
 * lose to a serial one.
 *
 * The short rows call sixteen times per timed op, for the reason change 261 established by
 * measuring it: an empty call through this harness costs 2.32 ns, which is most of what a 16-byte
 * CRC measures. Their labels say so.
 *
 * Every row prints the crc it computed, checked against the live export before anything is timed.
 * A checksum is the easiest thing in this project to get subtly wrong and the hardest to notice:
 * one wrong constant gives a perfectly plausible 32-bit number at a perfectly plausible speed.
 */
#include "bench.h"

typedef ULONG (NTAPI *F_Crc32)(const void*, SIZE_T, ULONG);

ULONG wia_crc32(const void*, SIZE_T, ULONG);
int wia_crc32_tables_init(void);
static F_Crc32 live;

typedef struct { const void* p; SIZE_T n; ULONG reps; } CTX;

static uint64_t op_ours(void* q)
{
    CTX* c = (CTX*)q;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) acc += wia_crc32(c->p, c->n, 0);
    return acc;
}
static uint64_t op_live(void* q)
{
    CTX* c = (CTX*)q;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) acc += live(c->p, c->n, 0);
    return acc;
}

#define MAXCASE 20
#define BIG     (1 << 20)
static unsigned char pool[BIG];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static void add(const char* label, size_t n)
{
    int k = nc;
    ctxs[k].p = pool;
    ctxs[k].n = n;
    ctxs[k].reps = (n <= 256) ? 16 : 1;
    cases[k].label  = label;
    cases[k].bytes  = n * ctxs[k].reps;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live = (F_Crc32)GetProcAddress(h, "RtlCrc32");
    if (!live) { printf("RtlCrc32 not found\n"); return 1; }
    if (wia_crc32_tables_init()) { printf("the shift tables failed their self-check\n"); return 1; }
    for (i = 0; i < BIG; ++i) pool[i] = (unsigned char)(i * 191 + 13);

    add("1 MB",                                  BIG);
    add("64 KB",                                 65536);
    add("8 KB",                                  8192);
    add("4 KB",                                  4096);
    add("3072 bytes (the LONG block, exactly)",  3072);
    add("3071 bytes (one short of it)",          3071);
    add("2048 bytes",                            2048);
    add("1024 bytes",                            1024);
    add("512 bytes",                             512);
    add("257 bytes",                             257);
    add("256 bytes (x16 calls)",                 256);
    add("192 bytes, the SHORT block (x16)",      192);
    add("191 bytes, one short of it (x16)",      191);
    add("128 bytes (x16 calls)",                 128);
    add("64 bytes (x16 calls)",                  64);
    add("32 bytes (x16 calls)",                  32);
    add("16 bytes (x16 calls)",                  16);
    add("8 bytes (x16 calls)",                   8);
    add("7 bytes, the byte tail (x16 calls)",    7);
    add("1 byte (x16 calls)",                    1);

    printf("== SUBJECTS (the CRC each row computes) ==\n");
    printf("  %-44s %9s   %-10s %s\n", "case", "bytes", "ours", "live");
    for (i = 0; i < nc; ++i) {
        ULONG ro = wia_crc32(ctxs[i].p, ctxs[i].n, 0);
        ULONG rl = live(ctxs[i].p, ctxs[i].n, 0);
        printf("  %-44s %9Iu   %08lX   %08lX%s\n", cases[i].label, ctxs[i].n,
               (unsigned long)ro, (unsigned long)rl, (ro == rl) ? "" : "   <== DISAGREE");
        if (ro != rl) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlCrc32", cases, nc, 25);
}
