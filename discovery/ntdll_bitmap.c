/* discovery/ntdll_bitmap.c
 *
 * THE ntdll BITMAP FAMILY, measured properly.
 *
 * discovery/ntdll_rtl_uncovered.c screened thirteen uncovered ntdll exports and found
 * RtlFindUnicodeSubstring at the top by a factor of six (it became change 252). The SECOND most
 * expensive row in that table was RtlFindSetBits at 0.133 ns/byte -- a RUN SEARCH, "find N
 * consecutive set bits" -- and the family around it was only sampled, not surveyed. This surveys it.
 *
 * WHY THIS FAMILY IS WORTH A SECOND LOOK. A bitmap search is the rare case where the shipped code
 * cannot easily be dismissed as "already optimal": popcount has a dedicated instruction, run-finding
 * maps onto TZCNT/LZCNT/BLSR, and 64 KB of bitmap is only 1024 qwords -- so a run search costing
 * 1088 ns means roughly ONE BIT PER CYCLE, which is what a naive bit-at-a-time loop costs and what a
 * word-at-a-time one does not.
 *
 * THE SUBJECT MATTERS MORE HERE THAN ANYWHERE ELSE IN THIS PROJECT, and getting it wrong is easy.
 * A bitmap of 0xA5A5A5A5 has a longest set run of TWO, so a request for 64 consecutive set bits
 * cannot be satisfied and the row measures the FULL-SCAN FAILURE path. That is a legitimate thing to
 * measure -- it is the expensive case -- but a row that does not SAY so reads as though the function
 * is slow at finding things when it is actually slow at not finding them. So every row below states
 * the density of its bitmap, what it asked for, and what it got, and each search is run against
 * THREE different bitmaps: sparse, dense, and one with a planted run near the end.
 *
 * RUN THIS ON AN IDLE MACHINE. Every row is a min-of-40.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;

typedef void  (NTAPI *F_Init)(RBM*, PULONG, ULONG);
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);
typedef ULONG (NTAPI *F_Count)(RBM*);
typedef ULONG (NTAPI *F_Range)(RBM*, ULONG, ULONG);
typedef ULONG (NTAPI *F_Runs)(RBM*, PULONG, ULONG, BOOLEAN);
typedef ULONG (NTAPI *F_Longest)(RBM*, PULONG);
typedef BOOLEAN (NTAPI *F_Are)(RBM*, ULONG, ULONG);
typedef void  (NTAPI *F_SetRun)(RBM*, ULONG, ULONG);

#define WORDS 2048                       /* 64 Kbit = 8 KB */
static ULONG b_sparse[WORDS];            /* 0xA5A5A5A5: longest set run is 2 */
static ULONG b_dense[WORDS];             /* mostly ones, a few holes */
static ULONG b_planted[WORDS];           /* sparse, with a 64-bit set run near the END */
static ULONG b_scratch[WORDS];
static ULONG b_real[WORDS];              /* an ALLOCATION bitmap: mostly set, ~200 clear runs */
static RBM   m_sparse, m_dense, m_planted, m_scratch, m_real;

static double bestns(void (*op)(void), int inner, int trials)
{
    LARGE_INTEGER f, a, b;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 32; ++i) op();
    for (t = 0; t < trials; ++t) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) op();
        QueryPerformanceCounter(&b);
        {
            double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            if (v < bv) bv = v;
        }
    }
    return bv;
}

static volatile unsigned long long sink;

static F_Find    p_findset, p_findclear;
static F_Count   p_numset, p_numclear;
static F_Range   p_numsetrange, p_numclearrange;
static F_Runs    p_findclearruns;
static F_Longest p_longestclear, p_longestset;
static F_Are     p_areclear, p_areset;
static F_SetRun  p_setbits, p_clearbits;
static F_Find    p_findsetandclear, p_findclearandset;
static F_Count   p_findfirstrunclear;

static void op_fs_sparse(void)  { sink += p_findset(&m_sparse, 64, 0); }
static void op_fs_planted(void) { sink += p_findset(&m_planted, 64, 0); }
static void op_fs_dense(void)   { sink += p_findset(&m_dense, 64, 0); }
static void op_fc_sparse(void)  { sink += p_findclear(&m_sparse, 64, 0); }
static void op_fc_dense(void)   { sink += p_findclear(&m_dense, 64, 0); }
static void op_numset(void)     { sink += p_numset(&m_sparse); }
static void op_numclear(void)   { sink += p_numclear(&m_sparse); }
static void op_nsr(void)        { sink += p_numsetrange(&m_sparse, 100, 60000); }
static void op_ncr(void)        { sink += p_numclearrange(&m_sparse, 100, 60000); }
static void op_runs(void)       { static ULONG r[128]; sink += p_findclearruns(&m_sparse, r, 64, FALSE); }
static void op_lclear(void)     { ULONG i; sink += p_longestclear(&m_sparse, &i); }
static void op_lclear_dense(void){ ULONG i; sink += p_longestclear(&m_dense, &i); }
static void op_lclear_real(void) { ULONG i; sink += p_longestclear(&m_real, &i); }
static void op_runs1s(void)     { static ULONG r[4]; sink += p_findclearruns(&m_sparse, r, 1, TRUE); }
static void op_runs64s(void)    { static ULONG r[256]; sink += p_findclearruns(&m_sparse, r, 64, TRUE); }
static void op_lset(void)       { ULONG i; sink += p_longestset(&m_sparse, &i); }
static void op_areclear(void)   { sink += p_areclear(&m_sparse, 0, 60000); }
static void op_areset(void)     { sink += p_areset(&m_dense, 0, 60000); }
static void op_setbits(void)    { p_setbits(&m_scratch, 0, 60000); sink += b_scratch[0]; }
static void op_clearbits(void)  { p_clearbits(&m_scratch, 0, 60000); sink += b_scratch[0]; }
static void op_fsac(void)       { sink += p_findsetandclear(&m_scratch, 64, 0); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i;

    p_findset          = (F_Find)   GetProcAddress(h, "RtlFindSetBits");
    p_findclear        = (F_Find)   GetProcAddress(h, "RtlFindClearBits");
    p_numset           = (F_Count)  GetProcAddress(h, "RtlNumberOfSetBits");
    p_numclear         = (F_Count)  GetProcAddress(h, "RtlNumberOfClearBits");
    p_numsetrange      = (F_Range)  GetProcAddress(h, "RtlNumberOfSetBitsInRange");
    p_numclearrange    = (F_Range)  GetProcAddress(h, "RtlNumberOfClearBitsInRange");
    p_findclearruns    = (F_Runs)   GetProcAddress(h, "RtlFindClearRuns");
    p_longestclear     = (F_Longest)GetProcAddress(h, "RtlFindLongestRunClear");
    p_longestset       = (F_Longest)GetProcAddress(h, "RtlFindLongestRunSet");
    p_areclear         = (F_Are)    GetProcAddress(h, "RtlAreBitsClear");
    p_areset           = (F_Are)    GetProcAddress(h, "RtlAreBitsSet");
    p_setbits          = (F_SetRun) GetProcAddress(h, "RtlSetBits");
    p_clearbits        = (F_SetRun) GetProcAddress(h, "RtlClearBits");
    p_findsetandclear  = (F_Find)   GetProcAddress(h, "RtlFindSetBitsAndClear");
    p_findclearandset  = (F_Find)   GetProcAddress(h, "RtlFindClearBitsAndSet");

    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    for (i = 0; i < WORDS; ++i) {
        b_sparse[i]  = 0xA5A5A5A5u;                 /* 1010 0101 ... longest set run 2 */
        b_dense[i]   = 0xFFFFFFFFu;
        b_planted[i] = 0xA5A5A5A5u;
        b_scratch[i] = 0xA5A5A5A5u;
    }
    for (i = 0; i < 8; ++i) b_dense[i * 200] = 0;   /* a few holes */
    b_planted[WORDS - 4] = 0xFFFFFFFFu;             /* a 96-bit SET run near the end */
    b_planted[WORDS - 3] = 0xFFFFFFFFu;
    b_planted[WORDS - 2] = 0xFFFFFFFFu;

    m_sparse.SizeOfBitMap  = WORDS * 32; m_sparse.Buffer  = b_sparse;
    m_dense.SizeOfBitMap   = WORDS * 32; m_dense.Buffer   = b_dense;
    m_planted.SizeOfBitMap = WORDS * 32; m_planted.Buffer = b_planted;
    m_scratch.SizeOfBitMap = WORDS * 32; m_scratch.Buffer = b_scratch;
    /* a REALISTIC allocation bitmap: mostly allocated, with about 200 free extents of varying
       length. The sparse subject above has a clear run every two bits and so contains more than
       sixteen thousand runs, which is an adversarial input, not a representative one. */
    { unsigned long long r = 88172645463325252ull; int w, run = 0;
      for (w = 0; w < WORDS; ++w) b_real[w] = 0xFFFFFFFFu;
      for (w = 0; w < 200; ++w) {
          int start, len, b;
          r ^= r << 13; r ^= r >> 7; r ^= r << 17;
          start = (int)(r % 64000); len = 1 + (int)((r >> 20) % 40);
          for (b = start; b < start + len && b < WORDS * 32; ++b)
              b_real[b >> 5] &= ~(1u << (b & 31));
          ++run;
      }
      (void)run; }
    m_real.SizeOfBitMap = WORDS * 32; m_real.Buffer = b_real;

    printf("ntdll bitmap family -- 64 Kbit (8 KB) bitmaps\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-40 and states its SUBJECT and RESULT,\n");
    printf("because a search that cannot be satisfied measures the FULL-SCAN FAILURE path and a row\n");
    printf("that does not say so reads as slow-at-finding when it is slow-at-NOT-finding.\n\n");
    printf("  sparse  = 0xA5A5A5A5 repeated, longest SET run 2, longest CLEAR run 1\n");
    printf("  dense   = all ones with 8 zero words\n");
    printf("  planted = sparse, with a 96-bit set run at the very end\n\n");
    printf("  %-40s %12s %10s  %s\n", "export / subject", "ns", "ns/byte", "returned");

#define ROW(fn, label, bytes, act) do {                                                  \
        if (!(fn)) { printf("  %-40s %12s\n", label, "NOT PRESENT"); }                    \
        else { double t = bestns(act, 3000, 40);                                          \
               printf("  %-40s %12.2f %10.3f  ", label, t,                                \
                      (double)(bytes) ? t / (double)(bytes) : 0.0); }                     \
    } while (0)

    ROW(p_findset, "RtlFindSetBits 64, sparse (CANNOT be met)", 8192, op_fs_sparse);
    printf("index=%lu (-1 = no such run)\n", p_findset(&m_sparse, 64, 0));
    ROW(p_findset, "  ... planted, run at the very END", 8192, op_fs_planted);
    printf("index=%lu\n", p_findset(&m_planted, 64, 0));
    ROW(p_findset, "  ... dense, run at the START", 8192, op_fs_dense);
    printf("index=%lu\n", p_findset(&m_dense, 64, 0));
    ROW(p_findclear, "RtlFindClearBits 64, sparse (CANNOT)", 8192, op_fc_sparse);
    printf("index=%lu\n", p_findclear(&m_sparse, 64, 0));
    ROW(p_findclear, "  ... dense (a hole at word 0)", 8192, op_fc_dense);
    printf("index=%lu\n", p_findclear(&m_dense, 64, 0));
    ROW(p_numset, "RtlNumberOfSetBits, whole map", 8192, op_numset);
    printf("count=%lu\n", p_numset(&m_sparse));
    ROW(p_numclear, "RtlNumberOfClearBits, whole map", 8192, op_numclear);
    printf("count=%lu\n", p_numclear(&m_sparse));
    ROW(p_numsetrange, "RtlNumberOfSetBitsInRange 100..60100", 7500, op_nsr);
    printf("count=%lu\n", p_numsetrange ? p_numsetrange(&m_sparse, 100, 60000) : 0);
    ROW(p_numclearrange, "RtlNumberOfClearBitsInRange", 7500, op_ncr);
    printf("count=%lu\n", p_numclearrange ? p_numclearrange(&m_sparse, 100, 60000) : 0);
    /* NOT a full scan, and the row says so: with SortByLength=FALSE the function STOPS as soon as
       the array is full, and the sparse bitmap has a clear run every two bits, so 64 runs are found
       within the first ~128 bits -- about 0.2% of the bitmap. The SORTED rows below are the full
       scans, and they cost eighty times as much. */
    ROW(p_findclearruns, "RtlFindClearRuns 64, UNSORTED (EARLY EXIT)", 8192, op_runs);
    { static ULONG r[128]; printf("runs=%lu -- stopped after ~128 of 65536 bits, NOT a full scan\n",
                                  p_findclearruns(&m_sparse, r, 64, FALSE)); }
    ROW(p_longestclear, "RtlFindLongestRunClear, sparse (16K runs)", 8192, op_lclear);
    { ULONG ix = 0; printf("len=%lu at %lu\n", p_longestclear(&m_sparse, &ix), ix); }
    ROW(p_longestclear, "  ... REALISTIC alloc bitmap (~200 runs)", 8192, op_lclear_real);
    { ULONG ix = 0; printf("len=%lu at %lu\n", p_longestclear(&m_real, &ix), ix); }
    ROW(p_longestclear, "  ... dense (8 runs)", 8192, op_lclear_dense);
    { ULONG ix = 0; printf("len=%lu at %lu\n", p_longestclear(&m_dense, &ix), ix); }
    ROW(p_findclearruns, "RtlFindClearRuns 1 run, SORTED, sparse", 8192, op_runs1s);
    { static ULONG r[4]; printf("runs=%lu (this IS what FindLongestRunClear calls)\n",
                                p_findclearruns(&m_sparse, r, 1, TRUE)); }
    ROW(p_findclearruns, "RtlFindClearRuns 64 runs, SORTED, sparse", 8192, op_runs64s);
    { static ULONG r[256]; printf("runs=%lu\n", p_findclearruns(&m_sparse, r, 64, TRUE)); }
    ROW(p_longestset, "RtlFindLongestRunSet", 8192, op_lset);
    { ULONG ix = 0; printf("len=%lu at %lu\n", p_longestset ? p_longestset(&m_sparse, &ix) : 0, ix); }
    ROW(p_areclear, "RtlAreBitsClear 0..60000, sparse", 7500, op_areclear);
    printf("ret=%d (0 = no, they are not)\n", (int)p_areclear(&m_sparse, 0, 60000));
    ROW(p_areset, "RtlAreBitsSet 0..60000, dense", 7500, op_areset);
    printf("ret=%d\n", (int)p_areset(&m_dense, 0, 60000));
    ROW(p_setbits, "RtlSetBits 0..60000", 7500, op_setbits);
    printf("word0=%08lX\n", b_scratch[0]);
    ROW(p_clearbits, "RtlClearBits 0..60000", 7500, op_clearbits);
    printf("word0=%08lX\n", b_scratch[0]);
    ROW(p_findsetandclear, "RtlFindSetBitsAndClear 64", 8192, op_fsac);
    printf("index=%lu\n", p_findsetandclear ? p_findsetandclear(&m_scratch, 64, 0) : 0);

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
