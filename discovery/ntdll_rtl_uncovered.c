/* discovery/ntdll_rtl_uncovered.c
 *
 * ntdll carries 69 landed changes and still has 191 uncovered Rtl* exports whose names suggest
 * string, buffer or bitmap work. This measures the subset that is plausibly BYTE-WISE with a
 * pinnable contract -- no locale, no code page, no grammar -- and ranks them by cost per byte, which
 * is the only number that decides whether a target is worth a contract derivation.
 *
 * What is deliberately not here. Anything whose name says locale or encoding (RtlIdnToUnicode,
 * RtlCustomCPToUnicodeN, RtlConsoleMultiByteToUnicodeN), anything that allocates as its main job
 * (the lookaside and memory-zone family), and the hash-table and memory-stream families, which are
 * data structures rather than byte loops.
 *
 * METHOD, and the two mistakes this file is written to avoid:
 *   * Run it on an idle machine. Every row is a min-of-N, which is robust to a slow sample but not
 *     to sustained load.
 *   * Every row prints what it actually did -- the return value and, where there is one, the output
 *     length. A survey row whose subject does not do the work its label claims is this project's
 *     most expensive recurring mistake: a subject whose escapable characters sat in the wrong URL
 *     segment made an earlier survey measure a no-op for a whole commit, and a 16 KB stack local
 *     moved an unrelated row by 2x. Both were invisible until the row was made to state itself.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } ASTR;
typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;

typedef PWSTR (NTAPI *F_FindSub)(USTR*, USTR*, BOOLEAN);
typedef LONG  (NTAPI *F_CmpU)(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);
typedef void  (NTAPI *F_InitA)(ASTR*, const char*);
typedef void  (NTAPI *F_InitU8)(ASTR*, const char*);
typedef void  (NTAPI *F_CopyU)(USTR*, USTR*);
typedef LONG  (NTAPI *F_AppS)(ASTR*, const ASTR*);
typedef ULONG (NTAPI *F_FindSet)(RBM*, ULONG, ULONG);
typedef ULONG (NTAPI *F_NumSet)(RBM*);
/* RtlCopyBitMap(Source, Destination, TargetBit, NumberOfBits) -- four parameters, source first.
   Called with three, it copied nothing: the row measured 1.98 ns and printed a destination first
   word of 00000000 against a source of A5A5A5A5. Caught by the row stating what it returned. */
typedef void  (NTAPI *F_CopyBM)(RBM*, RBM*, ULONG, ULONG);
typedef ULONG (NTAPI *F_FindRuns)(RBM*, PULONG, ULONG, BOOLEAN);

static double bestns(void (*op)(void), int inner, int trials)
{
    LARGE_INTEGER f, a, b;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 64; ++i) op();
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

/* ---- subjects ---------------------------------------------------------------------------- */
#define NW 4000
static wchar_t w1[NW + 8], w2[NW + 8], wneedle[64];
static char    a1[NW + 8], a2[NW + 8];
static ULONG   bmbuf[2048], bmbuf2[2048];
static RBM     bm, bm2;
static USTR    us_full, us_needle, us_dst, us_src;
static ASTR    as_dst, as_src, as_tmp;

static F_FindSub  p_findsub;
static F_CmpU     p_cmpu;
static F_InitA    p_inita;
static F_InitU8   p_initu8;
static F_CopyU    p_copyu;
static F_AppS     p_apps;
static F_FindSet  p_findset;
static F_NumSet   p_numset;
static F_NumSet   p_numclear;
static F_CopyBM   p_copybm;
static F_FindRuns p_findruns;

static void op_findsub_miss(void){ sink += (size_t)p_findsub(&us_full, &us_needle, FALSE); }
static void op_findsub_ci(void)  { sink += (size_t)p_findsub(&us_full, &us_needle, TRUE); }
/* RtlCompareUnicodeStrings counts CHARACTERS, not bytes. Passing NW*2 here made it walk 8000
   characters of a 4000-character buffer -- off the end of the subject and into whatever follows --
   and the row reported cmp=-25 for two IDENTICAL strings while still printing a plausible time.
   That is precisely the failure this file's own header warns about: a survey row whose subject does
   not do the work its label claims. The row printed what it returned, which is the only reason it
   was caught. */
static void op_cmpu_eq(void)     { sink += (unsigned)p_cmpu(w1, NW, w2, NW, FALSE); }
static void op_cmpu_ci(void)     { sink += (unsigned)p_cmpu(w1, NW, w2, NW, TRUE); }
static void op_inita(void)       { p_inita(&as_tmp, a1); sink += as_tmp.Length; }
static void op_initu8(void)      { p_initu8(&as_tmp, a1); sink += as_tmp.Length; }
static void op_copyu(void)       { p_copyu(&us_dst, &us_src); sink += us_dst.Length; }
static void op_apps(void)        { as_dst.Length = 0; sink += (unsigned)p_apps(&as_dst, &as_src); }
static void op_findset(void)     { sink += p_findset(&bm, 64, 0); }
static void op_numset(void)      { sink += p_numset(&bm); }
static void op_numclear(void)    { sink += p_numclear(&bm); }
static void op_copybm(void)      { p_copybm(&bm, &bm2, 0, bm.SizeOfBitMap); sink += bmbuf2[0]; }
static void op_findruns(void)    { static ULONG runs[64]; sink += p_findruns(&bm, runs, 32, FALSE); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i;

    p_findsub  = (F_FindSub) GetProcAddress(h, "RtlFindUnicodeSubstring");
    p_cmpu     = (F_CmpU)    GetProcAddress(h, "RtlCompareUnicodeStrings");
    p_inita    = (F_InitA)   GetProcAddress(h, "RtlInitAnsiString");
    p_initu8   = (F_InitU8)  GetProcAddress(h, "RtlInitUTF8String");
    p_copyu    = (F_CopyU)   GetProcAddress(h, "RtlCopyUnicodeString");
    p_apps     = (F_AppS)    GetProcAddress(h, "RtlAppendStringToString");
    p_findset  = (F_FindSet) GetProcAddress(h, "RtlFindSetBits");
    p_numset   = (F_NumSet)  GetProcAddress(h, "RtlNumberOfSetBits");
    p_numclear = (F_NumSet)  GetProcAddress(h, "RtlNumberOfClearBits");
    p_copybm   = (F_CopyBM)  GetProcAddress(h, "RtlCopyBitMap");
    p_findruns = (F_FindRuns)GetProcAddress(h, "RtlFindClearRuns");

    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    for (i = 0; i < NW; ++i) { w1[i] = (wchar_t)(L'a' + i % 26); w2[i] = w1[i]; }
    w1[NW] = 0; w2[NW] = 0;
    for (i = 0; i < NW; ++i) { a1[i] = (char)('a' + i % 26); a2[i] = a1[i]; }
    a1[NW] = 0; a2[NW] = 0;
    wcscpy(wneedle, L"zzzzzzzz");                       /* deliberately absent: the full scan */
    us_full.Buffer = w1;   us_full.Length = NW * 2;   us_full.MaximumLength = NW * 2;
    us_needle.Buffer = wneedle; us_needle.Length = 16; us_needle.MaximumLength = 16;
    us_src.Buffer = w1;    us_src.Length = NW * 2;    us_src.MaximumLength = NW * 2;
    us_dst.Buffer = w2;    us_dst.Length = 0;         us_dst.MaximumLength = NW * 2;
    as_src.Buffer = a1;    as_src.Length = NW;        as_src.MaximumLength = NW;
    as_dst.Buffer = a2;    as_dst.Length = 0;         as_dst.MaximumLength = NW;
    for (i = 0; i < 2048; ++i) { bmbuf[i] = 0xA5A5A5A5u; bmbuf2[i] = 0; }
    bm.SizeOfBitMap = 2048 * 32;  bm.Buffer = bmbuf;
    bm2.SizeOfBitMap = 2048 * 32; bm2.Buffer = bmbuf2;

    printf("ntdll Rtl* -- the uncovered, plausibly byte-wise exports\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-60 and states what it actually did.\n\n");
    printf("  %-34s %12s %10s  %s\n", "export / subject", "ns", "ns/byte", "what it returned");

#define ROW(fn, label, bytes, act) do {                                                   \
        if (!(fn)) { printf("  %-34s %12s\n", label, "NOT PRESENT"); }                     \
        else { double t = bestns(act, 20000, 60);                                          \
               printf("  %-34s %12.2f %10.3f  ", label, t, (double)(bytes) ? t / (double)(bytes) : 0.0); } \
    } while (0)

    ROW(p_findsub, "RtlFindUnicodeSubstring, miss", NW * 2, op_findsub_miss);
    printf("found=%p (NULL = scanned the whole string)\n", (void*)(size_t)sink);
    ROW(p_findsub, "  ... case-INSENSITIVE", NW * 2, op_findsub_ci);
    printf("(a case-insensitive flag is where a locale would hide)\n");
    ROW(p_cmpu, "RtlCompareUnicodeStrings, equal", NW * 2, op_cmpu_eq);
    printf("cmp=%d (0 = equal; the lengths are in CHARACTERS)\n",
           (int)p_cmpu(w1, NW, w2, NW, FALSE));
    ROW(p_cmpu, "  ... case-INSENSITIVE", NW * 2, op_cmpu_ci);
    printf("cmp=%d (0 = equal)\n", (int)p_cmpu(w1, NW, w2, NW, TRUE));
    ROW(p_inita, "RtlInitAnsiString, 4000 bytes", NW, op_inita);
    { p_inita(&as_tmp, a1); printf("Length=%u\n", as_tmp.Length); }
    ROW(p_initu8, "RtlInitUTF8String, 4000 bytes", NW, op_initu8);
    { p_initu8(&as_tmp, a1); printf("Length=%u\n", as_tmp.Length); }
    ROW(p_copyu, "RtlCopyUnicodeString, 4000 ch", NW * 2, op_copyu);
    { us_dst.Length = 0; p_copyu(&us_dst, &us_src); printf("Length=%u\n", us_dst.Length); }
    ROW(p_apps, "RtlAppendStringToString, 4000 B", NW, op_apps);
    { as_dst.Length = 0; p_apps(&as_dst, &as_src); printf("Length=%u\n", as_dst.Length); }
    /* 0xA5A5A5A5 is 1010 0101 repeating, so its longest run of set bits is TWO and a request for 64
       genuinely cannot be satisfied. index=4294967295 is the correct answer and it means this row
       measures the FULL-SCAN FAILURE path, which is the expensive one and the one worth knowing. */
    ROW(p_findset, "RtlFindSetBits, 64 in 64K bits", 8192, op_findset);
    printf("index=%lu (-1 = no such run exists; this row is the FULL scan)\n",
           p_findset(&bm, 64, 0));
    ROW(p_numset, "RtlNumberOfSetBits, 64K bits", 8192, op_numset);
    printf("count=%lu\n", p_numset(&bm));
    ROW(p_numclear, "RtlNumberOfClearBits, 64K bits", 8192, op_numclear);
    printf("count=%lu\n", p_numclear(&bm));
    ROW(p_copybm, "RtlCopyBitMap, 64K bits", 8192, op_copybm);
    printf("first word=%08lX\n", bmbuf2[0]);
    ROW(p_findruns, "RtlFindClearRuns, 64K bits", 8192, op_findruns);
    { static ULONG r[64]; printf("runs=%lu\n", p_findruns(&bm, r, 32, FALSE)); }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
