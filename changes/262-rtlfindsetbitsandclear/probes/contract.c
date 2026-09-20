/* changes/262-rtlfindsetbitsandclear/probes/contract.c
 *
 * What do ntdll!RtlFindSetBitsAndClear and ntdll!RtlFindClearBitsAndSet actually do?
 *
 * They are a search and a mutation in one call, and the mutation is the half that has no analogue
 * anywhere else in this project so far. Change 256 established the search half for the pure
 * RtlFindSetBits / RtlFindClearBits pair; it wraps, a run straddling the wrap point does not
 * count, a hint past the end is treated as zero, N = 0 returns the hint rounded down to a multiple
 * of eight, but NONE of that may be assumed here. The bitmap family has already produced two
 * cases where the obvious sibling rule was wrong (changes 123/124), and a function that MUTATES
 * has room for a whole class of rules the read-only one cannot have:
 *
 *   * does a NOT-FOUND call leave the bitmap completely alone?
 *   * does N = 0 mutate anything at the hint it returns?
 *   * are exactly N bits changed, or the whole run the search found?
 *   * is the mutation done even when the answer is the wrap-around one?
 *
 * Every question here is asked against a poisoned, fully recorded buffer and answered by diffing
 * the whole buffer afterwards, not by spot-checking the bits we expect to have changed. A function
 * that cleared one bit too many somewhere else in the bitmap would pass any check that only looked
 * where it was told to look.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_FindMut)(RBM*, ULONG, ULONG);

#define WORDS 64
static ULONG buf[WORDS], before[WORDS];
static RBM bm;

static void fill(ULONG v) { int i; for (i = 0; i < WORDS; ++i) buf[i] = v; }
static void setbit(ULONG i)   { buf[i >> 5] |=  (1u << (i & 31)); }
static void clearbit(ULONG i) { buf[i >> 5] &= ~(1u << (i & 31)); }
static void snap(void) { memcpy(before, buf, sizeof buf); }

/* Report every bit that changed, as runs, so "exactly N at the returned index" is visible rather
   than inferred. Returns the number of changed bits. */
static int diff(char* out, size_t cap)
{
    int i, n = 0;
    size_t used = 0;
    int run_start = -1;
    out[0] = 0;
    for (i = 0; i <= (int)(WORDS * 32); ++i) {
        int changed = 0;
        if (i < (int)(WORDS * 32)) {
            ULONG a = (before[i >> 5] >> (i & 31)) & 1u;
            ULONG b = (buf[i >> 5] >> (i & 31)) & 1u;
            changed = (a != b);
        }
        if (changed) { ++n; if (run_start < 0) run_start = i; }
        else if (run_start >= 0) {
            if (used < cap - 32)
                used += (size_t)sprintf(out + used, "%s%d..%d", used ? ", " : "", run_start, i - 1);
            run_start = -1;
        }
    }
    if (!n) strcpy(out, "nothing");
    return n;
}

static void ask(F_FindMut f, const char* name, const char* what, ULONG n, ULONG hint)
{
    char d[512];
    ULONG r;
    int changed;
    snap();
    r = f(&bm, n, hint);
    changed = diff(d, sizeof d);
    printf("  %-14s %-44s N=%-5lu hint=%-5lu -> ", name, what, n, hint);
    if (r == 0xFFFFFFFFul) printf("NOT FOUND");
    else                   printf("%-9lu", r);
    printf("   changed %d bit(s): %s\n", changed, d);
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_FindMut fsac = (F_FindMut)GetProcAddress(h, "RtlFindSetBitsAndClear");
    F_FindMut fcas = (F_FindMut)GetProcAddress(h, "RtlFindClearBitsAndSet");
    if (!fsac || !fcas) { printf("resolve failed\n"); return 1; }
    bm.Buffer = buf;
    bm.SizeOfBitMap = WORDS * 32;
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== RtlFindSetBitsAndClear / RtlFindClearBitsAndSet: the contract ==\n");
    printf("   every line diffs the WHOLE buffer, so a bit changed anywhere else would show\n\n");

    printf("-- 1. the basic shape: find N, and then what exactly is written? --\n");
    {
        int i;
        fill(0); for (i = 40; i < 60; ++i) setbit((ULONG)i);   /* a run of 20 set bits at 40 */
        ask(fsac, "FindSet&Clear", "a run of 20 set at 40, asking for 8", 8, 0);
        fill(0); for (i = 40; i < 60; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "the same, asking for exactly 20", 20, 0);
        fill(0); for (i = 40; i < 60; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "the same, asking for 21", 21, 0);

        fill(0xFFFFFFFFu); for (i = 40; i < 60; ++i) clearbit((ULONG)i);
        ask(fcas, "FindClear&Set", "a run of 20 clear at 40, asking for 8", 8, 0);
        fill(0xFFFFFFFFu); for (i = 40; i < 60; ++i) clearbit((ULONG)i);
        ask(fcas, "FindClear&Set", "the same, asking for exactly 20", 20, 0);
    }

    printf("\n-- 2. NOT FOUND: is the bitmap left completely alone? --\n");
    fill(0);            ask(fsac, "FindSet&Clear", "nothing set anywhere", 8, 0);
    fill(0xFFFFFFFFu);  ask(fcas, "FindClear&Set", "everything set, nothing clear", 8, 0);
    {
        int i;
        fill(0); for (i = 40; i < 47; ++i) setbit((ULONG)i);   /* 7 set: one short */
        ask(fsac, "FindSet&Clear", "a run of SEVEN when eight are wanted", 8, 0);
    }

    printf("\n-- 3. does the WRAP happen here too, and does it mutate when it does? --\n");
    {
        int i;
        fill(0); for (i = 10; i < 20; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "only run at 10, hint 300 (a wrap, if it wraps)", 8, 300);
        fill(0);
        for (i = 10; i < 20; ++i) setbit((ULONG)i);
        for (i = 400; i < 410; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "runs at 10 AND 400, hint 300", 8, 300);
        fill(0xFFFFFFFFu); for (i = 10; i < 20; ++i) clearbit((ULONG)i);
        ask(fcas, "FindClear&Set", "only run at 10, hint 300", 8, 300);
    }

    printf("\n-- 4. a run straddling the wrap point --\n");
    {
        int i;
        fill(0);
        for (i = 2044; i < 2048; ++i) setbit((ULONG)i);
        for (i = 0; i < 4; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "4 set at 2044 and 4 at 0, hint 2000, asking 8", 8, 2000);
    }

    printf("\n-- 5. N = 0 -- change 256 found it returns the hint rounded DOWN to a multiple of 8 --\n");
    fill(0xA5A5A5A5u); ask(fsac, "FindSet&Clear", "N=0, hint 0", 0, 0);
    fill(0xA5A5A5A5u); ask(fsac, "FindSet&Clear", "N=0, hint 7", 0, 7);
    fill(0xA5A5A5A5u); ask(fsac, "FindSet&Clear", "N=0, hint 100", 0, 100);
    fill(0xA5A5A5A5u); ask(fcas, "FindClear&Set", "N=0, hint 100", 0, 100);
    fill(0xA5A5A5A5u); ask(fsac, "FindSet&Clear", "N=0, hint past the end", 0, 9999);

    printf("\n-- 6. a hint at or past SizeOfBitMap --\n");
    {
        int i;
        fill(0); for (i = 10; i < 20; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "hint exactly SizeOfBitMap", 8, WORDS * 32);
        fill(0); for (i = 10; i < 20; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "hint far past the end", 8, 999999);
    }

    printf("\n-- 7. N larger than the bitmap, and the slack past SizeOfBitMap --\n");
    fill(0xFFFFFFFFu);
    bm.SizeOfBitMap = 40;
    ask(fsac, "FindSet&Clear", "all ones, declared 40 bits, asking 40", 40, 0);
    fill(0xFFFFFFFFu);
    ask(fsac, "FindSet&Clear", "the same, asking 41 (the slack is really set)", 41, 0);
    fill(0);
    ask(fcas, "FindClear&Set", "all zero, declared 40 bits, asking 41", 41, 0);
    bm.SizeOfBitMap = WORDS * 32;

    printf("\n-- 8. the mutation at the very edges of the bitmap --\n");
    {
        int i;
        fill(0); for (i = 0; i < 8; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "the run is bits 0..7", 8, 0);
        bm.SizeOfBitMap = 100;
        fill(0); for (i = 92; i < 100; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "the run ENDS at SizeOfBitMap (100)", 8, 0);
        bm.SizeOfBitMap = WORDS * 32;
    }

    printf("\n-- 9. calling twice: the second call must not find what the first consumed --\n");
    {
        int i;
        fill(0); for (i = 40; i < 60; ++i) setbit((ULONG)i);
        ask(fsac, "FindSet&Clear", "first call, asking 8", 8, 0);
        ask(fsac, "FindSet&Clear", "second call on the SAME bitmap", 8, 0);
        ask(fsac, "FindSet&Clear", "third call -- only four are left", 8, 0);
    }
    return 0;
}
