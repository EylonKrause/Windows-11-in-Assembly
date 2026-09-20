/* discovery/ntdll_bitmap3.c
 *
 * The bitmap mutators, and three other scans -- a third sweep of ntdll.
 *
 * Enumerating ntdll's exports against this project's manifest leaves 154 uncovered names that are
 * plausibly byte-wise. This measures the ones the two earlier bitmap sweeps never touched, because
 * those sweeps went after SEARCHES and every function here WRITES or merely scans:
 *
 *      RtlSetBits / RtlClearBits          set or clear a RANGE of bits
 *      RtlSetAllBits / RtlClearAllBits    ... all of them
 *      RtlSetBit / RtlClearBit / RtlTestBit   one bit, which is the floor for a call
 *      RtlIsZeroMemory                    is this buffer entirely zero
 *      RtlCrc32 / RtlComputeCrc32         a checksum, where change 076 already did the 64-bit one
 *
 * Why the range mutators are interesting here specifically: change 262 already built and gated a
 * bit-range set/clear -- masked word at each end, whole words between, 32 bytes at a time -- as the
 * mutation half of RtlFindSetBitsAndClear. If ntdll's standalone RtlSetBits is a loop, that code is
 * already written and already proved against a live export.
 *
 * METHOD, and the mistakes this file is written to avoid:
 *   * Run it on an idle machine. Every row is a min-of-N.
 *   * Every row prints what it actually did. a survey row whose subject does not do the work its
 *     label claims is this project's most expensive recurring mistake.
 *   * a mutator cannot be timed by calling it repeatedly on the same subject unless the call is
 *     idempotent. Setting the same range twice writes the same bytes, so these particular mutators
 *     ARE idempotent and the loop is honest -- which is stated here rather than assumed, because
 *     change 262's benchmark had to be built entirely around the cases where it is false.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;

typedef void    (NTAPI *F_Range)(RBM*, ULONG, ULONG);
typedef void    (NTAPI *F_All)(RBM*);
typedef void    (NTAPI *F_One)(RBM*, ULONG);
typedef BOOLEAN (NTAPI *F_Test)(RBM*, ULONG);
typedef BOOLEAN (NTAPI *F_IsZero)(const void*, SIZE_T);
/* The two crc entry points take their arguments in the opposite order, which is worth stating
   because calling one with the other's convention passes 0 as the buffer and faults -- which is
   exactly what the first run of this file did. */
typedef ULONG   (NTAPI *F_Crc32)(const void*, SIZE_T, ULONG);        /* RtlCrc32 */
typedef ULONG   (NTAPI *F_Compute32)(ULONG, const void*, SIZE_T);    /* RtlComputeCrc32 */

static double freq;
static uint64_t sink;

static double timeit(void (*op)(void), int reps)
{
    LARGE_INTEGER a, b;
    double best = 1e300;
    int t, i;
    for (i = 0; i < 8; ++i) op();
    for (t = 0; t < 40; ++t) {
        double ns;
        QueryPerformanceCounter(&a);
        for (i = 0; i < reps; ++i) op();
        QueryPerformanceCounter(&b);
        ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq / (double)reps;
        if (ns < best) best = ns;
    }
    return best;
}

static F_Range  p_setbits, p_clearbits;
static F_All    p_setall, p_clearall;
static F_One    p_setbit, p_clearbit;
static F_Test   p_testbit;
static F_IsZero p_iszero;
static F_Crc32     p_crc32;
static F_Compute32 p_computecrc32;

#define WORDS 2048                     /* 64 Kbit */
static ULONG  bits[WORDS];
static char   zerobuf[65536];
static RBM    bm;
static char   whatsit[160];

static void op_setbits(void)   { p_setbits(&bm, 100, 60000); }
static void op_clearbits(void) { p_clearbits(&bm, 100, 60000); }
static void op_setbits_s(void) { p_setbits(&bm, 100, 40); }
static void op_clearbits_s(void){ p_clearbits(&bm, 100, 40); }
static void op_setall(void)    { p_setall(&bm); }
static void op_clearall(void)  { p_clearall(&bm); }
static void op_setbit(void)    { p_setbit(&bm, 12345); }
static void op_clearbit(void)  { p_clearbit(&bm, 12345); }
static void op_testbit(void)   { sink += p_testbit(&bm, 12345); }
static void op_iszero(void)    { sink += p_iszero(zerobuf, 65536); }
static void op_iszero_s(void)  { sink += p_iszero(zerobuf, 64); }
static void op_iszero_late(void)
{
    zerobuf[65000] = 1;
    sink += p_iszero(zerobuf, 65536);
    zerobuf[65000] = 0;
}
static void op_crc32(void)     { sink += p_crc32(zerobuf, 65536, 0); }
static void op_computecrc32(void) { sink += p_computecrc32(0, zerobuf, 65536); }

#define ROW(fn, label, bytes, op, reps)                                                     \
    do {                                                                                    \
        if (!(fn)) { printf("  %-46s  NOT EXPORTED\n", label); break; }                     \
        { double ns = timeit(op, reps);                                                     \
          if ((bytes) > 0)                                                                  \
              printf("  %-46s %9.2f %8.3f  %s\n", label, ns, ns / (double)(bytes), whatsit); \
          else                                                                              \
              printf("  %-46s %9.2f %8s  %s\n", label, ns, "per call", whatsit); }           \
    } while (0)

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    LARGE_INTEGER f;
    int i;
    QueryPerformanceFrequency(&f);
    freq = (double)f.QuadPart;
    setvbuf(stdout, NULL, _IONBF, 0);

    p_setbits   = (F_Range) GetProcAddress(h, "RtlSetBits");
    p_clearbits = (F_Range) GetProcAddress(h, "RtlClearBits");
    p_setall    = (F_All)   GetProcAddress(h, "RtlSetAllBits");
    p_clearall  = (F_All)   GetProcAddress(h, "RtlClearAllBits");
    p_setbit    = (F_One)   GetProcAddress(h, "RtlSetBit");
    p_clearbit  = (F_One)   GetProcAddress(h, "RtlClearBit");
    p_testbit   = (F_Test)  GetProcAddress(h, "RtlTestBit");
    p_iszero    = (F_IsZero)GetProcAddress(h, "RtlIsZeroMemory");
    p_crc32     = (F_Crc32) GetProcAddress(h, "RtlCrc32");
    p_computecrc32 = (F_Compute32)GetProcAddress(h, "RtlComputeCrc32");

    bm.SizeOfBitMap = WORDS * 32;
    bm.Buffer = bits;
    for (i = 0; i < WORDS; ++i) bits[i] = 0xA5A5A5A5u;

    printf("ntdll bitmap MUTATORS and three other scans -- a third sweep\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-40 and states what it did.\n");
    printf("These mutators are IDEMPOTENT -- setting the same range twice writes the same bytes --\n");
    printf("so timing them in a loop is honest, which change 262's subjects were not.\n\n");
    printf("  %-46s %9s %8s  %s\n", "export / subject", "ns", "ns/byte", "what it did");

    sprintf(whatsit, "60000 bits = 7500 bytes touched");
    ROW(p_setbits,   "RtlSetBits, 60000 bits",        7500, op_setbits,   2000);
    ROW(p_clearbits, "RtlClearBits, 60000 bits",      7500, op_clearbits, 2000);
    sprintf(whatsit, "40 bits, which is one or two words");
    ROW(p_setbits,   "RtlSetBits, 40 bits",              5, op_setbits_s,   20000);
    ROW(p_clearbits, "RtlClearBits, 40 bits",            5, op_clearbits_s, 20000);
    sprintf(whatsit, "the whole 64 Kbit bitmap");
    ROW(p_setall,    "RtlSetAllBits, 64 Kbit",         8192, op_setall,   5000);
    ROW(p_clearall,  "RtlClearAllBits, 64 Kbit",       8192, op_clearall, 5000);
    sprintf(whatsit, "a single bit: the floor for a call");
    ROW(p_setbit,    "RtlSetBit",                         0, op_setbit,   50000);
    ROW(p_clearbit,  "RtlClearBit",                       0, op_clearbit, 50000);
    ROW(p_testbit,   "RtlTestBit",                        0, op_testbit,  50000);

    for (i = 0; i < 65536; ++i) zerobuf[i] = 0;
    sprintf(whatsit, "all zero, so the whole buffer is read");
    ROW(p_iszero, "RtlIsZeroMemory, 64 KB all zero",  65536, op_iszero,      2000);
    sprintf(whatsit, "a non-zero byte at 65000: nearly a full scan");
    ROW(p_iszero, "RtlIsZeroMemory, 64 KB, byte at 65000", 65536, op_iszero_late, 2000);
    sprintf(whatsit, "64 bytes, all zero");
    ROW(p_iszero, "RtlIsZeroMemory, 64 bytes",           64, op_iszero_s,   50000);

    sprintf(whatsit, "crc=%08lX", (unsigned long)(p_crc32 ? p_crc32(zerobuf, 65536, 0) : 0));
    ROW(p_crc32,        "RtlCrc32, 64 KB",            65536, op_crc32,        2000);
    sprintf(whatsit, "crc=%08lX", (unsigned long)(p_computecrc32 ? p_computecrc32(0, zerobuf, 65536) : 0));
    ROW(p_computecrc32, "RtlComputeCrc32, 64 KB",     65536, op_computecrc32, 2000);

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
