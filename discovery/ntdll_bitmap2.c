/* discovery/ntdll_bitmap2.c
 *
 * THE REST OF THE ntdll BITMAP FAMILY. discovery/ntdll_bitmap.c timed the searches and the counts,
 * and four changes came out of it -- 255 (RtlFindLongestRunClear), 256 (RtlFindSetBits and
 * RtlFindClearBits), 257 (the RtlNumberOfSetBits family) and 258 (RtlFindClearRuns). What it did
 * NOT time is everything else the family exports, and two of its rows asked a question that let the
 * subject answer immediately, which is not the same as timing the function.
 *
 * THE TWO ROWS THAT WERE TOO EASY. `RtlAreBitsClear 0..60000, sparse` measured 1.60 ns and
 * `RtlAreBitsSet 0..60000, dense` 2.20 ns -- and both returned 0, meaning NO. A range check that
 * answers "no" stops at the first bit that disagrees, which on those subjects is within the first
 * word: those rows timed a two-word function. THE EXPENSIVE CASE IS THE ONE THAT SAYS YES, because
 * saying yes means every bit in the range was examined. Both forms are measured here, and every row
 * says which answer it got.
 *
 * WHAT ELSE IS IN THE FAMILY, and none of it has been timed:
 *
 *   RtlFindNextForwardRunClear      the run finder that walks FORWARD from an index
 *   RtlFindLastBackwardRunClear     ... and the one that walks BACKWARD, which no change here has
 *                                   needed yet and which cannot reuse a forward scan's shape
 *   RtlFindSetBitsAndClear          a search and a mutation in one call
 *   RtlFindClearBitsAndSet
 *   RtlCopyBitMap                   a bit-granular copy between bitmaps: FOUR arguments, not three
 *   RtlExtractBitMap                the same, extracting a sub-bitmap
 *   RtlSetAllBits / RtlClearAllBits a whole-bitmap fill
 *   RtlNumberOfSetBitsUlongPtr      one word, no bitmap at all
 *
 * EVERY ROW PRINTS WHAT IT RETURNED. A range check that answers "no" and a range check that answers
 * "yes" are different functions wearing one name; a search that finds in the first word and one that
 * fails over 64 Kbit are different functions wearing one name. A table that did not say which it
 * timed would be measuring the subject and reporting it as the code.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;

typedef BOOLEAN (NTAPI *F_Are)(RBM*, ULONG, ULONG);
typedef ULONG   (NTAPI *F_Run)(RBM*, ULONG, PULONG);
typedef ULONG   (NTAPI *F_Find)(RBM*, ULONG, ULONG);
typedef VOID    (NTAPI *F_Copy)(RBM*, RBM*, ULONG, ULONG);
typedef VOID    (NTAPI *F_All)(RBM*);
typedef ULONG   (NTAPI *F_Pop)(ULONG_PTR);
typedef VOID    (NTAPI *F_Init)(RBM*, PULONG, ULONG);

static F_Are  p_areset, p_areclear;
static F_Run  p_nextfwd, p_lastback;
static F_Find p_fsac, p_fcas;
static F_Copy p_copy, p_extract;
static F_All  p_setall, p_clrall;
static F_Pop  p_popptr;
static F_Init p_init;

#define WORDS 2048                      /* 64 Kbit */
static ULONG a[WORDS], b[WORDS], scratch[WORDS];
static RBM bm, bm2;
static volatile uint64_t sink;
static char note[128];

static double freq(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }

static double timeit(void (*op)(void), int inner)
{
    LARGE_INTEGER x, y; int t, i; double best = 1e30, fr = freq();
    for (i = 0; i < 8; ++i) op();
    for (t = 0; t < 40; ++t) {
        QueryPerformanceCounter(&x);
        for (i = 0; i < inner; ++i) op();
        QueryPerformanceCounter(&y);
        { double v = (double)(y.QuadPart - x.QuadPart) * 1e9 / fr / (double)inner;
          if (v < best) best = v; }
    }
    return best;
}

/* ---- the fills each row needs ---- */
static void fill_ones(void)   { int i; for (i = 0; i < WORDS; ++i) a[i] = 0xFFFFFFFFu; }
static void fill_zeros(void)  { int i; for (i = 0; i < WORDS; ++i) a[i] = 0u; }
static void fill_sparse(void) { int i; for (i = 0; i < WORDS; ++i) a[i] = 0xA5A5A5A5u; }

/* ---- the operations ---- */
static void op_areset_yes(void)   { sink += p_areset(&bm, 0, 60000); }
static void op_areset_no(void)    { sink += p_areset(&bm, 0, 60000); }
static void op_areclear_yes(void) { sink += p_areclear(&bm, 0, 60000); }
static void op_nextfwd(void)      { ULONG s = 0; sink += p_nextfwd(&bm, 1, &s) + s; }
static void op_nextfwd_far(void)  { ULONG s = 0; sink += p_nextfwd(&bm, 60000, &s) + s; }
static void op_lastback(void)     { ULONG s = 0; sink += p_lastback(&bm, 65535, &s) + s; }
static void op_lastback_near(void){ ULONG s = 0; sink += p_lastback(&bm, 100, &s) + s; }
static void op_fsac(void)         { sink += p_fsac(&bm, 64, 0); }
static void op_fcas(void)         { sink += p_fcas(&bm, 64, 0); }
static void op_copy(void)         { p_copy(&bm, &bm2, 0, 65536); sink += b[0]; }
static void op_copy_off(void)     { p_copy(&bm, &bm2, 3, 65500); sink += b[0]; }
static void op_extract(void)      { p_extract(&bm, &bm2, 5, 65000); sink += b[0]; }
static void op_setall(void)       { p_setall(&bm); sink += a[0]; }
static void op_clrall(void)       { p_clrall(&bm); sink += a[0]; }
static void op_popptr(void)       { sink += p_popptr((ULONG_PTR)sink | 0xA5A5A5A5A5A5A5A5ull); }

#define ROW(fn, label, bytes, op, inner) do {                                            \
        if (!(fn)) { printf("  %-46s %s\n", label, "NOT PRESENT"); }                      \
        else { double ns = timeit(op, inner);                                             \
               printf("  %-46s %9.2f  %7.3f  %s\n", label, ns,                            \
                      (bytes) ? ns / (double)(bytes) : 0.0, note); }                      \
    } while (0)

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    ULONG s = 0, r;

    p_areset   = (F_Are) GetProcAddress(h, "RtlAreBitsSet");
    p_areclear = (F_Are) GetProcAddress(h, "RtlAreBitsClear");
    p_nextfwd  = (F_Run) GetProcAddress(h, "RtlFindNextForwardRunClear");
    p_lastback = (F_Run) GetProcAddress(h, "RtlFindLastBackwardRunClear");
    p_fsac     = (F_Find)GetProcAddress(h, "RtlFindSetBitsAndClear");
    p_fcas     = (F_Find)GetProcAddress(h, "RtlFindClearBitsAndSet");
    p_copy     = (F_Copy)GetProcAddress(h, "RtlCopyBitMap");
    p_extract  = (F_Copy)GetProcAddress(h, "RtlExtractBitMap");
    p_setall   = (F_All) GetProcAddress(h, "RtlSetAllBits");
    p_clrall   = (F_All) GetProcAddress(h, "RtlClearAllBits");
    p_popptr   = (F_Pop) GetProcAddress(h, "RtlNumberOfSetBitsUlongPtr");
    p_init     = (F_Init)GetProcAddress(h, "RtlInitializeBitMap");

    SetThreadAffinityMask(GetCurrentThread(), 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    setvbuf(stdout, NULL, _IONBF, 0);

    bm.SizeOfBitMap = WORDS * 32; bm.Buffer = a;
    bm2.SizeOfBitMap = WORDS * 32; bm2.Buffer = b;

    printf("the REST of the ntdll bitmap family -- 64 Kbit (8 KB) bitmaps\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-40 and states WHAT IT RETURNED,\n");
    printf("because a range check that answers NO stops at the first disagreeing bit and a search\n");
    printf("that succeeds in the first word never scans -- neither is the function's real cost.\n\n");
    printf("  %-46s %9s  %7s  %s\n", "export / subject", "ns", "ns/byte", "returned");

    /* ---- the range checks, BOTH answers ---- */
    fill_ones();
    r = p_areset(&bm, 0, 60000);
    sprintf(note, "ret=%lu (%s) -- every bit examined", r, r ? "YES" : "no");
    ROW(p_areset, "RtlAreBitsSet 0..60000, all ones (YES)", 7500, op_areset_yes, 2000);

    fill_sparse();
    r = p_areset(&bm, 0, 60000);
    sprintf(note, "ret=%lu (%s) -- stops at the first clear bit", r, r ? "YES" : "no");
    ROW(p_areset, "RtlAreBitsSet 0..60000, sparse (no)", 7500, op_areset_no, 20000);

    fill_zeros();
    r = p_areclear(&bm, 0, 60000);
    sprintf(note, "ret=%lu (%s) -- every bit examined", r, r ? "YES" : "no");
    ROW(p_areclear, "RtlAreBitsClear 0..60000, all zero (YES)", 7500, op_areclear_yes, 2000);

    /* ---- the directional run finders ---- */
    fill_ones();
    a[1000] = 0u;                                   /* one hole, deliberately far away */
    { ULONG st = 0; r = p_nextfwd(&bm, 1, &st);
      sprintf(note, "len=%lu at %lu -- scans to the hole", r, st); }
    ROW(p_nextfwd, "RtlFindNextForwardRunClear from 1", 4000, op_nextfwd, 2000);

    { ULONG st = 0; r = p_nextfwd(&bm, 60000, &st);
      sprintf(note, "len=%lu at %lu", r, st); }
    ROW(p_nextfwd, "  ... from 60000 (no hole ahead)", 700, op_nextfwd_far, 20000);

    { ULONG st = 0; r = p_lastback(&bm, 65535, &st);
      sprintf(note, "len=%lu at %lu -- scans BACK to the hole", r, st); }
    ROW(p_lastback, "RtlFindLastBackwardRunClear from 65535", 8000, op_lastback, 2000);

    { ULONG st = 0; r = p_lastback(&bm, 100, &st);
      sprintf(note, "len=%lu at %lu", r, st); }
    ROW(p_lastback, "  ... from 100 (the hole is ahead, not behind)", 12, op_lastback_near, 20000);

    /* ---- search and mutate ---- */
    fill_sparse();
    r = p_fsac(&bm, 64, 0);
    sprintf(note, "index=%lu (%s)", r, r == 0xFFFFFFFFu ? "not found -- a FULL scan" : "found");
    ROW(p_fsac, "RtlFindSetBitsAndClear 64, sparse", 8192, op_fsac, 2000);

    fill_sparse();
    r = p_fcas(&bm, 64, 0);
    sprintf(note, "index=%lu (%s)", r, r == 0xFFFFFFFFu ? "not found -- a FULL scan" : "found");
    ROW(p_fcas, "RtlFindClearBitsAndSet 64, sparse", 8192, op_fcas, 2000);

    /* ---- the bulk moves ---- */
    fill_sparse();
    if (p_copy) { p_copy(&bm, &bm2, 0, 65536); sprintf(note, "dst[0]=%08lX (aligned copy)", b[0]); }
    ROW(p_copy, "RtlCopyBitMap 65536 bits, target 0", 8192, op_copy, 2000);

    if (p_copy) { p_copy(&bm, &bm2, 3, 65500); sprintf(note, "dst[0]=%08lX (UNALIGNED target)", b[0]); }
    ROW(p_copy, "  ... target 3: every bit shifted", 8192, op_copy_off, 2000);

    if (p_extract) { p_extract(&bm, &bm2, 5, 65000); sprintf(note, "dst[0]=%08lX", b[0]); }
    ROW(p_extract, "RtlExtractBitMap 65000 bits from 5", 8125, op_extract, 2000);

    sprintf(note, "a whole-bitmap fill");
    ROW(p_setall, "RtlSetAllBits 64 Kbit", 8192, op_setall, 2000);
    ROW(p_clrall, "RtlClearAllBits 64 Kbit", 8192, op_clrall, 2000);

    sprintf(note, "one word, no bitmap");
    ROW(p_popptr, "RtlNumberOfSetBitsUlongPtr", 8, op_popptr, 50000);

    printf("\n  RtlInitializeBitMap %s\n", p_init ? "present (setup only, not timed)" : "NOT PRESENT");
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
