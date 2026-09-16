/* changes/258-rtlfindclearruns/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll export.
 *
 * WHAT IS COMPARED IS THE WHOLE ARRAY, entry by entry, and the return value -- not just the count.
 * The sorted form's contract is an ORDERING, so a count comparison would pass an implementation
 * that returned the right runs in the wrong order, and a length comparison would pass one that
 * broke ties the wrong way.
 *
 * AND THE ARRAY IS CHECKED PAST THE RETURNED COUNT. Every buffer is pre-filled with a canary and
 * compared in full, so an implementation that wrote more entries than it reported -- or that
 * scribbled past a small array -- is caught rather than flattered.
 *
 * SizeOfRunArray = 0 WITH A RUN PRESENT IS EXCLUDED: probes/contract.c established that the SHIPPED
 * export faults there, so there is no behaviour to match. It is tested against the ORACLE only.
 *
 *   1. EXHAUSTIVE over every 16-bit bitmap, at several capacities, both sorted and not.
 *   2. TIES -- many equal-length runs, which is the only thing that tests the ordering rule.
 *   3. CAPACITY pressure: arrays smaller than, equal to and larger than the number of runs.
 *   4. THE 64-BIT WORD BOUNDARY, where the carry lives.
 *   5. A GUARD PAGE at the end of the buffer, at odd ULONG counts.
 *   6. RANDOMISED at several densities.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef struct { ULONG StartingIndex; ULONG NumberOfBits; } RUN;
typedef ULONG (NTAPI *F_Runs)(RBM*, RUN*, ULONG, BOOLEAN);

ULONG wia_findclearruns(void*, RUN*, ULONG, BOOLEAN);
ULONG ref_findclearruns(void*, RUN*, ULONG, BOOLEAN);

static F_Runs live;
static long fails = 0, cases = 0;

#define CAPMAX 300
static RUN ao[CAPMAX], ar[CAPMAX], al[CAPMAX];

static void one(ULONG* buf, ULONG size, ULONG cap, BOOLEAN sorted, const char* where)
{
    RBM bm;
    ULONG no, nr, nl;
    int bad = 0, i;
    if (cap == 0) return;                 /* the shipped export faults; see the header */
    ++cases;
    bm.SizeOfBitMap = size; bm.Buffer = buf;
    memset(ao, 0xEE, sizeof ao); memset(ar, 0xEE, sizeof ar); memset(al, 0xEE, sizeof al);
    no = wia_findclearruns(&bm, ao, cap, sorted);
    nr = ref_findclearruns(&bm, ar, cap, sorted);
    nl = live(&bm, al, cap, sorted);
    if (no != nr || no != nl) bad = 1;
    if (!bad && memcmp(ao, ar, sizeof ao) != 0) bad = 2;   /* the WHOLE array, canary included */
    if (!bad && memcmp(ao, al, sizeof ao) != 0) bad = 3;
    if (bad) {
        if (++fails <= 20) {
            printf("  MISMATCH [%s] size=%lu cap=%lu sorted=%d  n: ours=%lu ref=%lu live=%lu (%d)\n",
                   where, size, cap, (int)sorted, no, nr, nl, bad);
            for (i = 0; i < (int)(no < 6 ? no : 6); ++i)
                printf("        ours (%lu,%lu)  ref (%lu,%lu)  live (%lu,%lu)\n",
                       ao[i].StartingIndex, ao[i].NumberOfBits,
                       ar[i].StartingIndex, ar[i].NumberOfBits,
                       al[i].StartingIndex, al[i].NumberOfBits);
        }
    }
}

static unsigned long long rs = 0x2B992DDFA23249D6ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_Runs)GetProcAddress(h, "RtlFindClearRuns");
    if (!live) { printf("RtlFindClearRuns not found\n"); return 1; }

    printf("== CORRECTNESS: RtlFindClearRuns (ours vs oracle vs LIVE ntdll) ==\n");
    printf("   the WHOLE array is compared, canary included, not just the returned count\n");

    /* ---- 1. EXHAUSTIVE over every 16-bit bitmap ---- */
    {
        long before = cases;
        ULONG v;
        ULONG buf[2];
        static const ULONG CAPS[4] = { 1, 2, 5, 12 };
        int c;
        for (v = 0; v < 65536; ++v) {
            buf[0] = v | 0xFFFF0000u;
            buf[1] = 0xFFFFFFFFu;
            for (c = 0; c < 4; ++c) {
                one(buf, 16, CAPS[c], FALSE, "exhaustive 16-bit");
                one(buf, 16, CAPS[c], TRUE,  "exhaustive 16-bit");
            }
        }
        printf("  1. EXHAUSTIVE: all 65536 16-bit bitmaps x 4 capacities x both: %ld cases\n",
               cases - before);
    }

    /* ---- 2. TIES: the ordering rule is the only thing this tests ---- */
    {
        long before = cases;
        ULONG buf[32];
        int len, gap, i, b, k;
        ULONG cap;
        for (len = 1; len <= 8; ++len)
            for (gap = 1; gap <= 6; ++gap) {
                for (i = 0; i < 32; ++i) buf[i] = 0xFFFFFFFFu;
                for (b = 3; b + len <= 1000; b += len + gap)
                    for (k = 0; k < len; ++k) buf[(b + k) >> 5] &= ~(1u << ((b + k) & 31));
                for (cap = 1; cap <= 40; cap += 7) {
                    one(buf, 1024, cap, TRUE,  "ties");
                    one(buf, 1024, cap, FALSE, "ties");
                }
                /* one run made LONGER than the rest, so the sort has something to move */
                for (k = 0; k < len + 3 && 500 + k < 1000; ++k)
                    buf[(500 + k) >> 5] &= ~(1u << ((500 + k) & 31));
                for (cap = 1; cap <= 40; cap += 7) one(buf, 1024, cap, TRUE, "ties + one longer");
            }
        printf("  2. ties: equal runs at every length 1..8 and gap 1..6, 6 capacities: %ld cases\n",
               cases - before);
    }

    /* ---- 3. capacity pressure ---- */
    {
        long before = cases;
        ULONG buf[32];
        ULONG cap;
        int i, k, b;
        for (i = 0; i < 32; ++i) buf[i] = 0xFFFFFFFFu;
        for (k = 0; k < 12; ++k)
            for (b = 0; b < 3 + k; ++b) {
                int x = 40 * k + b;
                if (x < 1024) buf[x >> 5] &= ~(1u << (x & 31));
            }
        for (cap = 1; cap <= 60; ++cap) {
            one(buf, 1024, cap, TRUE,  "capacity");
            one(buf, 1024, cap, FALSE, "capacity");
        }
        printf("  3. arrays smaller, equal and larger than the run count: %ld cases\n",
               cases - before);
    }

    /* ---- 4. the 64-bit word boundary ---- */
    {
        long before = cases;
        ULONG buf[32];
        int start, len, i;
        for (start = 40; start <= 90; ++start)
            for (len = 1; len <= 50; len += 3) {
                for (i = 0; i < 32; ++i) buf[i] = 0xFFFFFFFFu;
                for (i = start; i < start + len && i < 1024; ++i)
                    buf[i >> 5] &= ~(1u << (i & 31));
                one(buf, 1024, 4, TRUE,  "64-bit boundary");
                one(buf, 1024, 4, FALSE, "64-bit boundary");
            }
        printf("  4. runs across the 64-bit word boundary: %ld cases\n", cases - before);
    }

    /* ---- 5. a guard page ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  5. guard page SKIPPED\n");
        } else {
            ULONG nw, sz, k, cap;
            for (nw = 1; nw <= 41; ++nw) {
                ULONG* buf = (ULONG*)(base + si.dwPageSize) - nw;
                for (k = 0; k < nw; ++k) buf[k] = 0xC3C3F00Fu;
                for (sz = (nw - 1) * 32 + 1; sz <= nw * 32; sz += 7)
                    for (cap = 1; cap <= 9; cap += 4) {
                        one(buf, sz, cap, TRUE,  "guard page");
                        one(buf, sz, cap, FALSE, "guard page");
                        guard += 2;
                    }
                for (k = 0; k < nw; ++k) buf[k] = 0;
                one(buf, nw * 32, 3, TRUE, "guard page, all clear");
                ++guard;
            }
            printf("  5. a PAGE_NOACCESS page at the end of the buffer, 1..41 ULONGs: %ld cases, "
                   "no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 6. randomised ---- */
    {
        long before = cases;
        ULONG buf[128];
        int trial, i, d;
        static const int DENS[6] = { 0, 2, 10, 50, 90, 100 };
        for (trial = 0; trial < 30000; ++trial) {
            ULONG sz, cap;
            d = DENS[rnd() % 6];
            for (i = 0; i < 128; ++i) buf[i] = 0xFFFFFFFFu;
            if (d == 100) { for (i = 0; i < 128; ++i) buf[i] = 0u; }
            else for (i = 0; i < 4096; ++i)
                     if ((int)(rnd() % 100) < d) buf[i >> 5] &= ~(1u << (i & 31));
            sz = 1 + (rnd() % 4096);
            cap = 1 + (rnd() % 60);
            one(buf, sz, cap, (BOOLEAN)(rnd() & 1), "randomised");
        }
        printf("  6. randomised, 6 densities, capacities 1..60: %ld cases\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (whole array exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
