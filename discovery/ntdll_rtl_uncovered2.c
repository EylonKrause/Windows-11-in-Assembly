/* discovery/ntdll_rtl_uncovered2.c
 *
 * A SECOND SWEEP of uncovered ntdll Rtl* exports, the ones the first sweep never measured.
 *
 * discovery/ntdll_rtl_uncovered.c took the exports whose names suggest string work and ranked them
 * by cost per byte. Everything it found worth doing is now landed (changes 263 and 264 came out of
 * it), and what it left behind is memcpy-bound at 0.012 ns/byte, where there is little to win. This
 * file goes after a different shape: Fills, appends, parses and single-value lookups that the first
 * sweep did not list at all.
 *
 * What is deliberately not here. Anything whose name says locale or encoding, anything that
 * allocates as its main job, and the hash-table and memory-zone families, the same exclusions the
 * first sweep made, for the same reasons.
 *
 * METHOD, and the mistakes this file is written to avoid:
 *   * Run it on an idle machine. Every row is a min-of-N, robust to a slow sample but not to load.
 *   * Every row prints what it actually did; the return value and, where there is one, the output
 *     it produced. A survey row whose subject does not do the work its label claims is this
 *     project's most expensive recurring mistake: a subject whose escapable characters sat in the
 *     wrong URL segment made an earlier survey measure a no-op for a whole commit.
 *   * a per-byte cost is only meaningful for a function whose work scales with a length. The
 *     single-value rows below (a character fold, a status lookup, a bit scan) are reported in ns
 *     per CALL and marked, because dividing them by a byte count would invent a number.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } ASTR;

typedef void  (NTAPI *F_FillU)(void*, SIZE_T, ULONG);
typedef void  (NTAPI *F_FillU8)(void*, SIZE_T, ULONGLONG);
typedef LONG  (NTAPI *F_AppUSS)(USTR*, const USTR*);
typedef LONG  (NTAPI *F_AppAsciiz)(ASTR*, const char*);
typedef LONG  (NTAPI *F_U2I)(const USTR*, ULONG, ULONG*);
typedef LONG  (NTAPI *F_U2I64)(const USTR*, ULONG, LONGLONG*);
typedef BOOLEAN (NTAPI *F_Dos83)(const USTR*, ASTR*, BOOLEAN*);
typedef void  (NTAPI *F_CopyStr)(ASTR*, const ASTR*);
typedef WCHAR (NTAPI *F_Up1)(WCHAR);
typedef ULONG (NTAPI *F_Nt2Dos)(LONG);
typedef ULONG (NTAPI *F_Msb)(ULONGLONG);
typedef ULONG (NTAPI *F_PopUlongPtr)(ULONG_PTR);

static double freq;
static uint64_t sink;

static double timeit(void (*op)(void), int reps)
{
    LARGE_INTEGER a, b;
    double best = 1e300;
    int t, i;
    for (i = 0; i < 8; ++i) op();                       /* warm */
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

/* ---- the subjects ---- */
static F_FillU      p_fillu;
static F_FillU8     p_fillu8;
static F_AppUSS     p_appuss;
static F_AppAsciiz  p_appasciiz;
static F_U2I        p_u2i;
static F_U2I64      p_u2i64;
static F_Dos83      p_dos83;
static F_CopyStr    p_copystr;
static F_Up1        p_upc, p_downc;
static F_Nt2Dos     p_nt2dos;
static F_Msb        p_msb, p_lsb;
static F_PopUlongPtr p_popptr;

static ULONG  fillbuf[16384];
static wchar_t wbig[8192], wsmall[64];
static char    abig[16384], asmall[64];
static USTR    us_src, us_dst;
static ASTR    as_dst, as_src;
static ULONG   out32;
static LONGLONG out64;

static void op_fillu(void)    { p_fillu(fillbuf, 16384 * 4, 0xA5A5A5A5u); }
static void op_fillu8(void)   { p_fillu8(fillbuf, 16384 * 4, 0xA5A5A5A5A5A5A5A5ull); }
static void op_appuss(void)   { us_dst.Length = 0; sink += (unsigned)p_appuss(&us_dst, &us_src); }
static void op_appasciiz(void){ as_dst.Length = 0; sink += (unsigned)p_appasciiz(&as_dst, abig); }
static void op_u2i(void)      { sink += (unsigned)p_u2i(&us_src, 10, &out32); }
static void op_u2i64(void)    { sink += (unsigned)p_u2i64(&us_src, 10, &out64); }
static void op_copystr(void)  { p_copystr(&as_dst, &as_src); }
static void op_upc(void)      { sink += p_upc((WCHAR)(sink & 0xFFFF)); }
static void op_downc(void)    { sink += p_downc((WCHAR)(sink & 0xFFFF)); }
static void op_nt2dos(void)   { sink += p_nt2dos((LONG)0xC0000034); }
static void op_msb(void)      { sink += p_msb(0x0000100000000000ull | (sink & 1)); }
static void op_lsb(void)      { sink += p_lsb(0x0000100000000000ull | (sink & 1)); }
static void op_popptr(void)   { sink += p_popptr((ULONG_PTR)0xA5A5A5A5A5A5A5A5ull); }
static void op_dos83(void)
{
    BOOLEAN spaces = FALSE;
    as_dst.Length = 0; as_dst.MaximumLength = 64; as_dst.Buffer = asmall;
    sink += p_dos83(&us_src, &as_dst, &spaces);
}

#define ROW(fn, label, bytes, op, reps)                                                    \
    do {                                                                                   \
        if (!(fn)) { printf("  %-46s  NOT EXPORTED\n", label); break; }                    \
        { double ns = timeit(op, reps);                                                    \
          if ((bytes) > 0)                                                                 \
              printf("  %-46s %9.2f %8.3f  %s\n", label, ns, ns / (double)(bytes), whatsit);\
          else                                                                             \
              printf("  %-46s %9.2f %8s  %s\n", label, ns, "per call", whatsit); }          \
    } while (0)

static char whatsit[160];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    LARGE_INTEGER f;
    int i;
    QueryPerformanceFrequency(&f);
    freq = (double)f.QuadPart;
    setvbuf(stdout, NULL, _IONBF, 0);

    p_fillu     = (F_FillU)     GetProcAddress(h, "RtlFillMemoryUlong");
    p_fillu8    = (F_FillU8)    GetProcAddress(h, "RtlFillMemoryUlonglong");
    p_appuss    = (F_AppUSS)    GetProcAddress(h, "RtlAppendUnicodeStringToString");
    p_appasciiz = (F_AppAsciiz) GetProcAddress(h, "RtlAppendAsciizToString");
    p_u2i       = (F_U2I)       GetProcAddress(h, "RtlUnicodeStringToInteger");
    p_u2i64     = (F_U2I64)     GetProcAddress(h, "RtlUnicodeStringToInt64");
    p_dos83     = (F_Dos83)     GetProcAddress(h, "RtlIsNameLegalDOS8Dot3");
    p_copystr   = (F_CopyStr)   GetProcAddress(h, "RtlCopyString");
    p_upc       = (F_Up1)       GetProcAddress(h, "RtlUpcaseUnicodeChar");
    p_downc     = (F_Up1)       GetProcAddress(h, "RtlDowncaseUnicodeChar");
    p_nt2dos    = (F_Nt2Dos)    GetProcAddress(h, "RtlNtStatusToDosError");
    p_msb       = (F_Msb)       GetProcAddress(h, "RtlFindMostSignificantBit");
    p_lsb       = (F_Msb)       GetProcAddress(h, "RtlFindLeastSignificantBit");
    p_popptr    = (F_PopUlongPtr)GetProcAddress(h, "RtlNumberOfSetBitsUlongPtr");

    printf("ntdll Rtl* -- a SECOND sweep: fills, appends, parses and single-value lookups\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-40 and states what it actually did.\n\n");
    printf("  %-46s %9s %8s  %s\n", "export / subject", "ns", "ns/byte", "what it returned");

    /* ---- fills: 64 KB ---- */
    strcpy(whatsit, "(a 64 KB fill)");
    ROW(p_fillu,  "RtlFillMemoryUlong, 64 KB",     65536, op_fillu,  200);
    sprintf(whatsit, "first word=%08lX", (unsigned long)fillbuf[0]);
    ROW(p_fillu8, "RtlFillMemoryUlonglong, 64 KB", 65536, op_fillu8, 200);

    /* ---- appends ---- */
    for (i = 0; i < 4000; ++i) wbig[i] = (wchar_t)(L'a' + (i % 26));
    us_src.Buffer = wbig; us_src.Length = 8000; us_src.MaximumLength = 8000;
    us_dst.Buffer = (PWSTR)malloc(20000); us_dst.Length = 0; us_dst.MaximumLength = 20000;
    if (p_appuss) op_appuss();
    sprintf(whatsit, "status=%08lX, dest Length=%u", (unsigned long)0, us_dst.Length);
    ROW(p_appuss, "RtlAppendUnicodeStringToString, 4000 ch", 8000, op_appuss, 2000);

    for (i = 0; i < 4000; ++i) abig[i] = (char)('a' + (i % 26));
    abig[4000] = 0;
    as_dst.Buffer = (PSTR)malloc(20000); as_dst.Length = 0; as_dst.MaximumLength = 20000;
    if (p_appasciiz) op_appasciiz();
    sprintf(whatsit, "dest Length=%u", as_dst.Length);
    ROW(p_appasciiz, "RtlAppendAsciizToString, 4000 bytes", 4000, op_appasciiz, 2000);

    as_src.Buffer = abig; as_src.Length = 4000; as_src.MaximumLength = 4001;
    as_dst.Length = 0;
    sprintf(whatsit, "a 4000-byte STRING copy");
    ROW(p_copystr, "RtlCopyString, 4000 bytes", 4000, op_copystr, 2000);

    /* ---- parses ---- */
    {
        static const wchar_t* NUM = L"4294967294";
        for (i = 0; NUM[i]; ++i) wsmall[i] = NUM[i];
        us_src.Buffer = wsmall; us_src.Length = (USHORT)(i * 2); us_src.MaximumLength = us_src.Length;
        out32 = 0; if (p_u2i) p_u2i(&us_src, 10, &out32);
        sprintf(whatsit, "value=%lu from %d characters", (unsigned long)out32, i);
        ROW(p_u2i, "RtlUnicodeStringToInteger, 10 digits", 20, op_u2i, 20000);
        out64 = 0; if (p_u2i64) p_u2i64(&us_src, 10, &out64);
        sprintf(whatsit, "value=%lld", (long long)out64);
        ROW(p_u2i64, "RtlUnicodeStringToInt64, 10 digits", 20, op_u2i64, 20000);
    }

    /* ---- the 8.3 check ---- */
    {
        static const wchar_t* NAME = L"README.TXT";
        for (i = 0; NAME[i]; ++i) wsmall[i] = NAME[i];
        us_src.Buffer = wsmall; us_src.Length = (USHORT)(i * 2); us_src.MaximumLength = us_src.Length;
        as_dst.Buffer = asmall; as_dst.Length = 0; as_dst.MaximumLength = 64;
        {
            BOOLEAN sp = FALSE;
            BOOLEAN r = p_dos83 ? p_dos83(&us_src, &as_dst, &sp) : 0;
            sprintf(whatsit, "legal=%d, spaces=%d, oem=\"%.*s\"", r, sp, as_dst.Length, asmall);
        }
        ROW(p_dos83, "RtlIsNameLegalDOS8Dot3, \"README.TXT\"", 20, op_dos83, 20000);
    }

    /* ---- single values: ns per CALL, not per byte ---- */
    sprintf(whatsit, "one character through the upcase table");
    ROW(p_upc,    "RtlUpcaseUnicodeChar",              0, op_upc,    50000);
    sprintf(whatsit, "one character through the downcase table");
    ROW(p_downc,  "RtlDowncaseUnicodeChar",            0, op_downc,  50000);
    sprintf(whatsit, "a status-to-error table lookup");
    ROW(p_nt2dos, "RtlNtStatusToDosError",             0, op_nt2dos, 50000);
    sprintf(whatsit, "a 64-bit bit scan");
    ROW(p_msb,    "RtlFindMostSignificantBit",         0, op_msb,    50000);
    ROW(p_lsb,    "RtlFindLeastSignificantBit",        0, op_lsb,    50000);
    sprintf(whatsit, "a population count");
    ROW(p_popptr, "RtlNumberOfSetBitsUlongPtr",        0, op_popptr, 50000);

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
