/* changes/260-rtlcopybitmap/bench.c
 *
 * OURS vs the LIVE ntdll!RtlCopyBitMap and ntdll!RtlExtractBitMap.
 *
 * THE TARGET OFFSET IS THE SUBJECT, not a parameter. discovery/ntdll_bitmap2.c found the shipped
 * copy EIGHTEEN TIMES apart on the same 8 KB depending on three bits of it:
 *
 *      RtlCopyBitMap 65536 bits, target 0     101.05 ns    0.012 ns/byte    RtlCopyMemory
 *        ... target 3                        1846.65 ns    0.225 ns/byte    a shifting loop
 *
 * so the rows below sweep the offset deliberately: 0 (byte-aligned, the memory-speed path), 8 and
 * 64 (byte- and word-aligned but not zero, so the copy MOVES but still does not shift), and 1, 3,
 * 7, 31 (every kind of shift). A table that reported one "copy" row would be averaging two
 * different functions.
 *
 * THE ALIGNED ROWS ARE THE HARD ONES AND THEY ARE KEPT. The shipped code reaches RtlCopyMemory
 * there and runs at memory speed; beating it is not the point and matching it is not free. They are
 * measured rather than quietly dropped, because a change that made the shifted copy ten times
 * faster and the aligned copy twice as slow would be a regression for most callers.
 *
 * SHORT COPIES ARE HERE FOR THE USUAL REASON: a copy of twenty bits is two masked words and no loop
 * at all, and that is where a vector implementation goes wrong.
 */
#include "bench.h"

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef VOID (NTAPI *F_Copy3)(RBM*, RBM*, ULONG);
typedef VOID (NTAPI *F_Copy4)(RBM*, RBM*, ULONG, ULONG);

VOID wia_copybitmap(void*, void*, ULONG);
VOID wia_extractbitmap(void*, void*, ULONG, ULONG);
static F_Copy3 live_copy;
static F_Copy4 live_extract;

typedef struct { RBM bs, bd; ULONG target, nbits; int extract; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    if (c->extract) wia_extractbitmap(&c->bs, &c->bd, c->target, c->nbits);
    else            wia_copybitmap(&c->bs, &c->bd, c->target);
    return c->bd.Buffer[0];
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    if (c->extract) live_extract(&c->bs, &c->bd, c->target, c->nbits);
    else            live_copy(&c->bs, &c->bd, c->target);
    return c->bd.Buffer[0];
}

#define MAXCASE 24
#define WORDS   2200                       /* room for 64 Kbit plus the shift */
static ULONG spool[MAXCASE][WORDS];
static ULONG dpool[MAXCASE][WORDS];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static void add(const char* label, ULONG ssize, ULONG dsize, ULONG target, ULONG nbits, int extract)
{
    int k = nc, i;
    for (i = 0; i < WORDS; ++i) {
        spool[k][i] = 0x12345678u + (ULONG)i * 0x9E3779B9u;
        dpool[k][i] = 0xCCCCCCCCu;
    }
    ctxs[k].bs.SizeOfBitMap = ssize; ctxs[k].bs.Buffer = spool[k];
    ctxs[k].bd.SizeOfBitMap = dsize; ctxs[k].bd.Buffer = dpool[k];
    ctxs[k].target = target; ctxs[k].nbits = nbits; ctxs[k].extract = extract;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)((ssize < dsize ? ssize : dsize) / 8);
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    static ULONG a[WORDS], b[WORDS];
    live_copy    = (F_Copy3)GetProcAddress(h, "RtlCopyBitMap");
    live_extract = (F_Copy4)GetProcAddress(h, "RtlExtractBitMap");
    if (!live_copy || !live_extract) { printf("RtlCopyBitMap not found\n"); return 1; }

    /*   label                                       ssize   dsize  target  nbits  extract */
    add("COPY 64 Kbit, target 0 (ALIGNED, memcpy)",  65536, 131072,      0,     0, 0);
    add("COPY 64 Kbit, target 8 (byte-aligned)",     65536, 131072,      8,     0, 0);
    add("COPY 64 Kbit, target 64 (word-aligned)",    65536, 131072,     64,     0, 0);
    add("COPY 64 Kbit, target 1 (SHIFTED)",          65536, 131072,      1,     0, 0);
    add("COPY 64 Kbit, target 3 (SHIFTED)",          65536, 131072,      3,     0, 0);
    add("COPY 64 Kbit, target 7 (SHIFTED)",          65536, 131072,      7,     0, 0);
    add("COPY 64 Kbit, target 31 (SHIFTED)",         65536, 131072,     31,     0, 0);
    add("COPY 8 Kbit, target 3 (SHIFTED)",            8192,  65536,      3,     0, 0);
    add("COPY 1 Kbit, target 3 (SHIFTED)",            1024,  65536,      3,     0, 0);
    add("COPY 256 bits, target 3 (SHIFTED)",           256,  65536,      3,     0, 0);
    add("COPY 64 bits, target 3 (SHIFTED)",             64,  65536,      3,     0, 0);
    add("COPY 20 bits, target 3 (one word)",            20,  65536,      3,     0, 0);
    add("COPY 64 Kbit into a SMALLER destination",   65536,  32768,      3,     0, 0);

    add("EXTRACT 64 Kbit from 0 (ALIGNED)",         131072,  65536,      0, 65536, 1);
    add("EXTRACT 64 Kbit from 8 (byte-aligned)",    131072,  65536,      8, 65536, 1);
    add("EXTRACT 64 Kbit from 3 (SHIFTED)",         131072,  65536,      3, 65536, 1);
    add("EXTRACT 64 Kbit from 31 (SHIFTED)",        131072,  65536,     31, 65536, 1);
    add("EXTRACT 8 Kbit from 3 (SHIFTED)",          131072,   8192,      3,  8192, 1);
    add("EXTRACT 1 Kbit from 3 (SHIFTED)",          131072,   1024,      3,  1024, 1);
    add("EXTRACT 64 bits from 3 (SHIFTED)",         131072,     64,      3,    64, 1);
    add("EXTRACT 20 bits from 3 (one word)",        131072,     20,      3,    20, 1);

    printf("== SUBJECTS (what each row actually writes) ==\n");
    printf("  %-44s %8s %7s   %-10s %-10s\n", "case", "ssize", "target", "ours dst[0]", "live dst[0]");
    for (i = 0; i < nc; ++i) {
        ULONG j;
        RBM bo, bl;
        for (j = 0; j < WORDS; ++j) { a[j] = 0xCCCCCCCCu; b[j] = 0xCCCCCCCCu; }
        bo = ctxs[i].bd; bo.Buffer = a;
        bl = ctxs[i].bd; bl.Buffer = b;
        if (ctxs[i].extract) {
            wia_extractbitmap(&ctxs[i].bs, &bo, ctxs[i].target, ctxs[i].nbits);
            live_extract(&ctxs[i].bs, &bl, ctxs[i].target, ctxs[i].nbits);
        } else {
            wia_copybitmap(&ctxs[i].bs, &bo, ctxs[i].target);
            live_copy(&ctxs[i].bs, &bl, ctxs[i].target);
        }
        printf("  %-44s %8lu %7lu   %08lX   %08lX%s\n", cases[i].label,
               ctxs[i].bs.SizeOfBitMap, ctxs[i].target, a[0], b[0],
               memcmp(a, b, sizeof a) == 0 ? "" : "   <== DISAGREE (whole buffer)");
        if (memcmp(a, b, sizeof a) != 0) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlCopyBitMap / RtlExtractBitMap", cases, nc, 25);
}
