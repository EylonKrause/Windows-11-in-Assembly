/* changes/258-rtlfindclearruns/probes/enumorder.c
 *
 * THE ORDER THE SHIPPED EXPORT FINDS RUNS IN -- which is NOT the order they occur in.
 *
 * probes/contract.c concluded that the UNSORTED form "returns the FIRST runs found, in order". The
 * first half is right and the second half is wrong, and the correctness corpus caught it: on a
 * sixteen-bit bitmap with clear runs at 1 (one bit), 3 (two bits) and 6 (ten bits), the shipped
 * export with SizeOfRunArray = 1 returns (3,2) -- not (1,1) -- and with room for all three:
 *
 *      (3,2)  (1,1)  (6,10)
 *
 * The contract probe missed it because every run in it was in a byte of its own, which is the one
 * arrangement where the two orders agree.
 *
 * WHY. ntdll!RtlFindClearRuns (RVA 0x0E3280) scans ONE BYTE AT A TIME through four byte tables:
 *
 *      RVA 0x17FBD0[b]  the number of CLEAR bits at the BOTTOM of b     (trailing zeros)
 *      RVA 0x192560[b]  the number of CLEAR bits at the TOP of b        (leading zeros)
 *      RVA 0x192548[n]  (1 << n) - 1
 *      RVA 0x180570[b]  the LENGTH OF THE LONGEST CLEAR RUN in b
 *      RVA 0x180558[n]  the mask of the bits at and above n -- used BOTH to force the slack past
 *                       SizeOfBitMap to ones AND, read backwards from 0x180560, as the top-n mask
 *
 * (Every one of those was dumped from the live image and checked against its claimed meaning over
 * all 256 byte values, not assumed from the shape of the code.)
 *
 * Per byte it does exactly this:
 *
 *   1. the run CARRIED IN from earlier bytes, plus this byte's trailing zeros, is now complete --
 *      emitted FIRST;
 *   2. the run at the TOP of the byte becomes the new carry -- NOT emitted here;
 *   3. both of those are masked off, and what is left -- the runs strictly INSIDE the byte -- is
 *      emitted by REPEATEDLY TAKING THE LONGEST ONE (0x180570), ties going to the lowest position,
 *      masking it off and going again.
 *
 * Step 3 is the whole discrepancy: a byte's interior runs come out LONGEST FIRST, so (3,2) precedes
 * (1,1). A byte holds at most three interior runs, so at most three entries are ever permuted, but
 * WHICH runs an undersized array keeps depends on it, and that is observable.
 *
 * AND THE SORTED FORM IS UNAFFECTED, which is worth stating because it is not obvious. Sorted output
 * is a STABLE sort of the found order by descending length, so the found order can only show through
 * between runs of EQUAL length -- and for two runs of equal length the found order and the ascending
 * order AGREE:
 *
 *      two runs of the same length L with starts s1 < s2 end at s1+L < s2+L, so the byte in which
 *      each completes is ordered the same way. If those bytes differ, the earlier one is emitted
 *      first. If they are the same byte, either both are interior -- equal length, so the tie goes
 *      to the lower position, which is s1 -- or one is the carry, and the carry starts at or below
 *      the byte's first bit while an interior run starts above it, so the carry is the one at s1
 *      and the carry is emitted first.
 *
 * So this probe checks TWO claims, against the live export rather than by argument:
 *   A. the model above reproduces the UNSORTED output exactly, entry for entry;
 *   B. a stable sort by descending length of the ASCENDING run order reproduces the SORTED output.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef struct { ULONG StartingIndex; ULONG NumberOfBits; } RUN;
typedef ULONG (NTAPI *F_Runs)(RBM*, RUN*, ULONG, BOOLEAN);

static F_Runs live;
static long cases = 0, bad_un = 0, bad_so = 0;

/* ---- the byte helpers, written the slow obvious way ---- */
static int tzc8(unsigned b) { int n = 0; while (n < 8 && !((b >> n) & 1)) ++n; return n; }
static int lzc8(unsigned b) { int n = 0; while (n < 8 && !((b >> (7 - n)) & 1)) ++n; return n; }
static int longest8(unsigned b)
{
    int best = 0, cur = 0, k;
    for (k = 0; k < 8; ++k) { if ((b >> k) & 1) cur = 0; else { ++cur; if (cur > best) best = cur; } }
    return best;
}

/* ---- the MODEL: the shipped scan, byte by byte, in the order it emits ---- */
#define MAXRUNS 70000
static RUN found[MAXRUNS];

static ULONG model_order(const ULONG* buf, ULONG size)
{
    ULONG n = 0, k, nbytes = (size + 7) >> 3;
    ULONG carry = 0, cstart = 0;
    for (k = 0; k < nbytes; ++k) {
        unsigned b = ((const unsigned char*)buf)[k], m;
        int tz, lz;
        if (k == nbytes - 1 && (size & 7))
            b |= (0xFFu << (size & 7)) & 0xFFu;               /* the slack reads as ONES */
        if (b == 0) { carry += 8; continue; }
        tz = tzc8(b);
        if (carry + (ULONG)tz) {                              /* 1. the carried run completes */
            if (n < MAXRUNS) { found[n].StartingIndex = cstart;
                               found[n].NumberOfBits = carry + tz; ++n; }
        }
        lz = lzc8(b);
        cstart = k * 8 + 8 - lz;                              /* 2. the top run becomes the carry */
        carry  = (ULONG)lz;
        m = b | ((1u << tz) - 1u) | ((lz ? (0xFFu << (8 - lz)) : 0u) & 0xFFu);
        while (m != 0xFF) {                                   /* 3. interior runs, LONGEST FIRST */
            int L = longest8(m), p = 0;
            unsigned w = (1u << L) - 1u;
            while (m & (w << p)) ++p;
            if (n < MAXRUNS) { found[n].StartingIndex = k * 8 + (ULONG)p;
                               found[n].NumberOfBits = (ULONG)L; ++n; }
            m |= (w << p) & 0xFFu;
        }
    }
    if (carry && n < MAXRUNS) { found[n].StartingIndex = cstart; found[n].NumberOfBits = carry; ++n; }
    return n;
}

/* ---- ascending order, for claim B ---- */
static ULONG ascending(const ULONG* buf, ULONG size, RUN* out)
{
    ULONG n = 0, i, cur = 0, start = 0;
    for (i = 0; i < size; ++i) {
        if (((buf[i >> 5] >> (i & 31)) & 1u) == 0) { if (!cur) start = i; ++cur; }
        else if (cur) { if (n < MAXRUNS) { out[n].StartingIndex = start; out[n].NumberOfBits = cur; ++n; }
                        cur = 0; }
    }
    if (cur && n < MAXRUNS) { out[n].StartingIndex = start; out[n].NumberOfBits = cur; ++n; }
    return n;
}

static RUN asc[MAXRUNS], want[300], got[300];

static void one(const ULONG* buf, ULONG size, ULONG cap, const char* where)
{
    RBM bm; ULONG nl, nw, i, n;
    bm.SizeOfBitMap = size; bm.Buffer = (PULONG)buf;
    ++cases;

    /* A. UNSORTED == the model order, truncated */
    n = model_order(buf, size);
    nw = (n < cap) ? n : cap;
    memset(want, 0xEE, sizeof want); memset(got, 0xEE, sizeof got);
    for (i = 0; i < nw; ++i) want[i] = found[i];
    nl = live(&bm, got, cap, FALSE);
    if (nl != nw || memcmp(want, got, sizeof want) != 0) {
        if (++bad_un <= 8) {
            printf("  UNSORTED MISMATCH [%s] size=%lu cap=%lu: model %lu, live %lu\n",
                   where, size, cap, nw, nl);
            for (i = 0; i < nw && i < 8; ++i)
                printf("        model (%lu,%lu)   live (%lu,%lu)\n",
                       want[i].StartingIndex, want[i].NumberOfBits,
                       got[i].StartingIndex,  got[i].NumberOfBits);
        }
    }

    /* B. SORTED == a stable sort by descending length of the ASCENDING order */
    n = ascending(buf, size, asc);
    for (i = 1; i < n; ++i) {
        RUN key = asc[i]; ULONG j = i;
        while (j > 0 && asc[j - 1].NumberOfBits < key.NumberOfBits) { asc[j] = asc[j - 1]; --j; }
        asc[j] = key;
    }
    nw = (n < cap) ? n : cap;
    memset(want, 0xEE, sizeof want); memset(got, 0xEE, sizeof got);
    for (i = 0; i < nw; ++i) want[i] = asc[i];
    nl = live(&bm, got, cap, TRUE);
    if (nl != nw || memcmp(want, got, sizeof want) != 0) {
        if (++bad_so <= 8) {
            printf("  SORTED MISMATCH [%s] size=%lu cap=%lu: model %lu, live %lu\n",
                   where, size, cap, nw, nl);
            for (i = 0; i < nw && i < 8; ++i)
                printf("        model (%lu,%lu)   live (%lu,%lu)\n",
                       want[i].StartingIndex, want[i].NumberOfBits,
                       got[i].StartingIndex,  got[i].NumberOfBits);
        }
    }
}

static unsigned long long rs = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    ULONG v, cap, buf[128];
    static const ULONG CAPS[7] = { 1, 2, 3, 5, 8, 17, 64 };
    int c, i, trial;
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_Runs)GetProcAddress(h, "RtlFindClearRuns");
    if (!live) { printf("RtlFindClearRuns not found\n"); return 1; }

    printf("RtlFindClearRuns -- THE ORDER RUNS ARE FOUND IN\n\n");

    /* the row that started it */
    {
        RUN o[8]; RBM bm; ULONG n, k;
        static const ULONG caps[3] = { 1, 2, 5 };
        buf[0] = 0xFFFF0025u; buf[1] = 0xFFFFFFFFu;    /* clear: 1 ; 3,4 ; 6..15 */
        bm.SizeOfBitMap = 16; bm.Buffer = buf;
        for (c = 0; c < 3; ++c) {
            memset(o, 0xEE, sizeof o);
            n = live(&bm, o, caps[c], FALSE);
            printf("   runs (1,1) (3,2) (6,10), UNSORTED, cap %lu -> %lu:", caps[c], n);
            for (k = 0; k < n; ++k) printf(" (%lu,%lu)", o[k].StartingIndex, o[k].NumberOfBits);
            printf("\n");
        }
        printf("   => the byte-interior runs come out LONGEST FIRST, not in position order\n\n");
    }

    /* 1. exhaustive 16-bit */
    for (v = 0; v < 65536; ++v) {
        buf[0] = v | 0xFFFF0000u; buf[1] = 0xFFFFFFFFu;
        for (c = 0; c < 7; ++c) one(buf, 16, CAPS[c], "exhaustive 16-bit");
    }
    printf("   1. all 65536 16-bit bitmaps x 7 capacities\n");

    /* 2. exhaustive 16-bit at every size 1..16, so the slack mask is in the model too */
    for (v = 0; v < 65536; ++v) {
        ULONG sz;
        buf[0] = v | 0xFFFF0000u; buf[1] = 0xFFFFFFFFu;
        for (sz = 1; sz <= 16; ++sz) one(buf, sz, 3, "16-bit x every size");
    }
    printf("   2. all 65536 16-bit bitmaps x every SizeOfBitMap 1..16\n");

    /* 3. randomised, every density, sizes on and off the byte boundary */
    for (trial = 0; trial < 60000; ++trial) {
        static const int DENS[6] = { 1, 3, 10, 50, 85, 99 };
        int d = DENS[rnd() % 6];
        ULONG sz;
        for (i = 0; i < 128; ++i) buf[i] = 0xFFFFFFFFu;
        for (i = 0; i < 4096; ++i) if ((int)(rnd() % 100) < d) buf[i >> 5] &= ~(1u << (i & 31));
        sz  = 1 + (rnd() % 4096);
        cap = 1 + (rnd() % 80);
        one(buf, sz, cap, "randomised");
    }
    printf("   3. 60000 randomised bitmaps, 6 densities, sizes 1..4096, capacities 1..80\n");

    printf("\n   %ld cases.  UNSORTED model: %ld mismatch(es).  SORTED model: %ld mismatch(es).\n",
           cases, bad_un, bad_so);
    printf(bad_un || bad_so ? "ENUM ORDER: the model is WRONG\n"
                            : "ENUM ORDER: CONFIRMED -- byte at a time, interior runs longest first;\n"
                              "            and the SORTED form is a stable sort of the ASCENDING order\n");
    return (bad_un || bad_so) ? 1 : 0;
}
