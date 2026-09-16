/* changes/260-rtlcopybitmap/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll exports, for BOTH RtlCopyBitMap and
 * RtlExtractBitMap.
 *
 * THESE FUNCTIONS MUTATE, so WHAT IS COMPARED IS THE WHOLE DESTINATION BUFFER, not the copied
 * range. Every case fills the destination with a poison pattern first and compares every byte of it
 * afterwards, because "the copy worked" and "the copy worked and also cleared the rest of the word"
 * are indistinguishable if only the copied range is examined. The bits before the range, the bits
 * after it, and the words past the end are all part of the contract.
 *
 * AND THE BUFFER IS LARGER THAN THE DECLARED BITMAP, deliberately. A TargetBit past the
 * destination's size makes the shipped export WRITE PAST SizeOfBitMap -- the count subtraction is
 * done in 32 bits and wraps -- so the corpus has to be able to see those writes rather than crash
 * on them, and the comparison has to cover them.
 *
 *   1. EXHAUSTIVE over every (target, source size) pair in a small window, both exports, with the
 *      source, destination and sizes all varied -- this is where every shift 0..31 and every
 *      combination of partial first and last word occurs.
 *   2. THE WRAP: a target at, just before and just past the destination's size.
 *   3. LONG copies at every shift, so the vector step runs many times and its seams with the head
 *      and tail words are exercised at every alignment.
 *   4. A GUARD PAGE after the SOURCE, at odd ULONG counts: the vector step reads 36 bytes of
 *      source for 32 of destination, and that must never reach the page after the array.
 *   5. A GUARD PAGE after the DESTINATION.
 *   6. RANDOMISED over sizes, targets and lengths.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef VOID (NTAPI *F_Copy3)(RBM*, RBM*, ULONG);
typedef VOID (NTAPI *F_Copy4)(RBM*, RBM*, ULONG, ULONG);

VOID wia_copybitmap(void*, void*, ULONG);
VOID wia_extractbitmap(void*, void*, ULONG, ULONG);
VOID ref_copybitmap(void*, void*, ULONG);
VOID ref_extractbitmap(void*, void*, ULONG, ULONG);

static F_Copy3 live_copy;
static F_Copy4 live_extract;
static long fails = 0, cases = 0, changed_cases = 0;

#define NW 96                                  /* words in each scratch buffer */
static ULONG src[NW], d_ours[NW], d_ref[NW], d_live[NW];

static unsigned long long rs = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

/* run one case three ways over an identical poisoned destination and compare every byte */
static void one(ULONG ssize, ULONG dsize, ULONG target, ULONG nbits, int extract, const char* where)
{
    RBM bs, bo, br, bl;
    int i;
    ++cases;
    for (i = 0; i < NW; ++i) { d_ours[i] = 0xCCCCCCCCu; d_ref[i] = 0xCCCCCCCCu; d_live[i] = 0xCCCCCCCCu; }
    bs.SizeOfBitMap = ssize; bs.Buffer = src;
    bo.SizeOfBitMap = dsize; bo.Buffer = d_ours;
    br.SizeOfBitMap = dsize; br.Buffer = d_ref;
    bl.SizeOfBitMap = dsize; bl.Buffer = d_live;
    if (extract) {
        wia_extractbitmap(&bs, &bo, target, nbits);
        ref_extractbitmap(&bs, &br, target, nbits);
        live_extract(&bs, &bl, target, nbits);
    } else {
        wia_copybitmap(&bs, &bo, target);
        ref_copybitmap(&bs, &br, target);
        live_copy(&bs, &bl, target);
    }
    for (i = 0; i < NW; ++i) if (d_live[i] != 0xCCCCCCCCu) { ++changed_cases; break; }
    if (memcmp(d_ours, d_live, sizeof d_ours) != 0 || memcmp(d_ref, d_live, sizeof d_ref) != 0) {
        if (++fails <= 15) {
            printf("  MISMATCH [%s] %s ssize=%lu dsize=%lu target=%lu nbits=%lu\n",
                   where, extract ? "EXTRACT" : "COPY", ssize, dsize, target, nbits);
            for (i = 0; i < 6; ++i)
                printf("        word %d: ours=%08lX ref=%08lX live=%08lX\n",
                       i, d_ours[i], d_ref[i], d_live[i]);
        }
    }
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    live_copy    = (F_Copy3)GetProcAddress(h, "RtlCopyBitMap");
    live_extract = (F_Copy4)GetProcAddress(h, "RtlExtractBitMap");
    if (!live_copy || !live_extract) { printf("resolve failed\n"); return 1; }
    for (i = 0; i < NW; ++i) src[i] = 0x12345678u + (ULONG)i * 0x9E3779B9u;

    printf("== CORRECTNESS: RtlCopyBitMap / RtlExtractBitMap (ours vs oracle vs LIVE ntdll) ==\n");
    printf("   the WHOLE destination buffer is compared, including the words past SizeOfBitMap\n");

    /* ---- 1. EXHAUSTIVE in a small window ---- */
    {
        long before = cases;
        ULONG t, ss, ds;
        for (t = 0; t <= 70; ++t)
            for (ss = 0; ss <= 70; ss += 3)
                for (ds = 64; ds <= 160; ds += 32) {
                    one(ss, ds, t, 0, 0, "exhaustive window");
                    one(ss, ds, t, ss, 1, "exhaustive window");
                }
        printf("  1. every target 0..70 x source size 0,3..69 x 4 destination sizes, both: %ld\n",
               cases - before);
    }

    /* ---- 2. the wrap ---- */
    {
        long before = cases;
        ULONG ds, t;
        for (ds = 32; ds <= 128; ds += 32)
            for (t = ds - 4; t <= ds + 40; ++t) {
                one(64, ds, t, 0, 0, "the wrap");
                one(64, ds, t, 64, 1, "the wrap");
            }
        printf("  2. a target at, before and past the destination size -- where the 32-bit\n"
               "     subtraction wraps and the shipped export writes past it: %ld\n", cases - before);
    }

    /* ---- 3. long copies at every shift ---- */
    {
        long before = cases;
        ULONG t, n;
        for (t = 0; t < 64; ++t)
            for (n = 900; n <= 1100; n += 37) {
                one(n, 2048, t, 0, 0, "long, every shift");
                one(2048, n, t, n, 1, "long, every shift");
            }
        printf("  3. copies of ~1000 bits at every target 0..63, so the vector step runs many\n"
               "     times and its seams are hit at every alignment: %ld\n", cases - before);
    }

    /* ---- 4. a guard page after the SOURCE ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  4. source guard page SKIPPED\n");
        } else {
            ULONG nw, k, t;
            RBM bs, bo, br, bl;
            for (nw = 1; nw <= 40; ++nw) {
                ULONG* s = (ULONG*)(base + si.dwPageSize) - nw;
                for (k = 0; k < nw; ++k) s[k] = 0xA5A5A5A5u + k;
                /* t < the source's size, always: a target PAST it makes the count wrap, and a
                   wrapped count reads words after the array -- which is the guard page here, and
                   which faults for the SHIPPED export too. That case is real and is covered by
                   corpus 2 over an ordinary buffer; it cannot be asked of a guarded one. */
                for (t = 0; t < 40 && t < nw * 32; ++t) {
                    int j;
                    for (j = 0; j < NW; ++j) { d_ours[j] = 0xCCCCCCCCu; d_ref[j] = 0xCCCCCCCCu; d_live[j] = 0xCCCCCCCCu; }
                    bs.SizeOfBitMap = nw * 32; bs.Buffer = s;
                    bo.SizeOfBitMap = 2048; bo.Buffer = d_ours;
                    br.SizeOfBitMap = 2048; br.Buffer = d_ref;
                    bl.SizeOfBitMap = 2048; bl.Buffer = d_live;
                    wia_copybitmap(&bs, &bo, t);
                    ref_copybitmap(&bs, &br, t);
                    live_copy(&bs, &bl, t);
                    ++cases; ++guard;
                    if (memcmp(d_ours, d_live, sizeof d_ours) || memcmp(d_ref, d_live, sizeof d_ref)) {
                        if (++fails <= 15)
                            printf("  MISMATCH [source guard] nw=%lu target=%lu\n", nw, t);
                    }
                    bo.SizeOfBitMap = 2048; bl.SizeOfBitMap = 2048; br.SizeOfBitMap = 2048;
                    for (j = 0; j < NW; ++j) { d_ours[j] = 0xCCCCCCCCu; d_ref[j] = 0xCCCCCCCCu; d_live[j] = 0xCCCCCCCCu; }
                    wia_extractbitmap(&bs, &bo, t, nw * 32);
                    ref_extractbitmap(&bs, &br, t, nw * 32);
                    live_extract(&bs, &bl, t, nw * 32);
                    ++cases; ++guard;
                    if (memcmp(d_ours, d_live, sizeof d_ours) || memcmp(d_ref, d_live, sizeof d_ref)) {
                        if (++fails <= 15)
                            printf("  MISMATCH [source guard, extract] nw=%lu target=%lu\n", nw, t);
                    }
                }
            }
            printf("  4. a PAGE_NOACCESS page right after the SOURCE, 1..40 ULONGs x 40 targets --\n"
                   "     the vector step reads 36 bytes for every 32 it writes: %ld cases, no fault\n",
                   guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 5. a guard page after the DESTINATION ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  5. destination guard page SKIPPED\n");
        } else {
            ULONG nw, t;
            RBM bs, bo, bl;
            for (nw = 1; nw <= 40; ++nw) {
                ULONG* d = (ULONG*)(base + si.dwPageSize) - nw;
                /* t < the destination's size, for the same reason as corpus 4: past it the
                   count wraps and the copy runs off the end of the buffer, which is the guard
                   page here and faults for the SHIPPED export too. */
                for (t = 0; t < 40 && t < nw * 32; ++t) {
                    ULONG k;
                    /* ours and the live export write the same words; compare them directly */
                    for (k = 0; k < nw; ++k) d[k] = 0xCCCCCCCCu;
                    bs.SizeOfBitMap = 200; bs.Buffer = src;
                    bo.SizeOfBitMap = nw * 32; bo.Buffer = d;
                    wia_copybitmap(&bs, &bo, t);
                    for (k = 0; k < nw; ++k) d_ours[k] = d[k];
                    for (k = 0; k < nw; ++k) d[k] = 0xCCCCCCCCu;
                    bl.SizeOfBitMap = nw * 32; bl.Buffer = d;
                    live_copy(&bs, &bl, t);
                    ++cases; ++guard;
                    if (memcmp(d_ours, d, (size_t)nw * 4) != 0) {
                        if (++fails <= 15)
                            printf("  MISMATCH [dest guard] nw=%lu target=%lu\n", nw, t);
                    }
                }
            }
            printf("  5. a PAGE_NOACCESS page right after the DESTINATION, 1..40 ULONGs x 40\n"
                   "     targets: %ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 6. randomised ---- */
    {
        long before = cases;
        int trial;
        /* EVERY PARAMETER IS BOUNDED SO THE WRAP CASES STAY INSIDE THE BUFFER. When the count
           wraps, the copy writes target + source-size bits, and that is NOT bounded by the declared
           destination size -- so with four scratch arrays laid out next to each other, a write that
           ran past one of them landed in the next and the harness then compared its own damage.
           2277 "mismatches" were exactly that, and none of them was a bug in the implementation.
           The buffers hold 3072 bits; nothing generated here can reach past 2048. */
        for (trial = 0; trial < 40000; ++trial) {
            ULONG ss = rnd() % 1024, ds = rnd() % 1024, t = rnd() % 1024, n = rnd() % 1024;
            for (i = 0; i < NW; ++i) src[i] = (ULONG)rnd();
            one(ss, ds, t, n, (int)(rnd() & 1), "randomised");
        }
        printf("  6. randomised sizes, targets and lengths, both exports: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  %ld of them actually wrote something -- a corpus whose every case copied nothing\n"
           "  would have proved only that four functions agree about doing nothing\n", changed_cases);
    printf(fails ? "CORRECTNESS: FAILED\n" : "CORRECTNESS: PASS (whole destination exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
