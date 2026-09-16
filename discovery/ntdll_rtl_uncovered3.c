/* discovery/ntdll_rtl_uncovered3.c
 *
 * A FOURTH SWEEP: the string/parse/compare exports the earlier ones did not reach.
 *
 * Enumerating ntdll's 2516 exports against this project's manifest leaves 154 uncovered names that
 * are plausibly byte-wise. The first sweep took the obvious string family, the second took fills
 * and appends, the third took the bitmap mutators and the checksums. What is left and still
 * plausible is measured here: name comparisons, a parse, a validator, the size calculators and the
 * UTF-8 string wrappers.
 *
 * WHAT IS DELIBERATELY NOT HERE, and why -- so that nobody measures them again hoping for a
 * different answer:
 *
 *   * RtlComputeCrc32 measured 0.012 ns/byte in the third sweep, which is 81 GB/s. That is faster
 *     than three parallel hardware CRC32 chains can go, so it is already a carry-less-multiply
 *     fold. There is nothing to take.
 *   * RtlIsNameLegalDOS8Dot3 produces an OEM string, which is a code-page conversion, and this
 *     project does not reproduce code-page tables.
 *   * RtlNtStatusToDosError is a lookup into a table of several thousand status codes. It could
 *     only be reproduced by transcribing Windows data, and its domain cannot be enumerated to
 *     build the table from the OS the way changes 210 and 267 build theirs.
 *   * RtlNormalizeString and RtlIsNormalizedString are Unicode normalisation: locale-adjacent by
 *     definition, and the same question that ended lstrcmp_is_linguistic.c.
 *
 * WHAT THIS SWEEP CONCLUDED, so that nobody has to run it again:
 *
 *   * RtlUnicodeStringToUTF8String (0.099 ns/byte) and RtlUTF8StringToUnicodeString (0.213) are
 *     REAL TARGETS, and this project already converted the N-forms they wrap, as changes 016 and
 *     034, at 2.82x and 3.12x.
 *   * RtlEqualComputerName and RtlEqualDomainName are slow -- 559 ns for fifteen characters -- and
 *     NOT CONVERTIBLE: their fold is an OEM code-page conversion rather than the upcase table. The
 *     evidence is beside their rows.
 *   * RtlValidateUnicodeString, the three size calculators, RtlUnicodeStringToInteger, RtlRandom
 *     and RtlRandomEx are all a couple of nanoseconds and none of them scales with a length. There
 *     is nothing in them to take.
 *   * RtlStringFromGUID allocates.
 *
 * METHOD, and the mistakes this file is written to avoid:
 *   * RUN IT ON AN IDLE MACHINE. Every row is a min-of-N.
 *   * EVERY ROW PRINTS WHAT IT ACTUALLY DID -- the return value and the output where there is one.
 *     A survey row whose subject does not do the work its label claims is this project's most
 *     expensive recurring mistake.
 *   * A ROW WHOSE COST DOES NOT SCALE WITH A LENGTH is reported per CALL and marked, because
 *     dividing it by a byte count invents a number.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } ASTR;

typedef LONG    (NTAPI *F_U2I)(const USTR*, ULONG, ULONG*);
typedef BOOLEAN (NTAPI *F_EqName)(const USTR*, const USTR*);
typedef LONG    (NTAPI *F_Validate)(ULONG, const USTR*, ULONG*);
typedef ULONG   (NTAPI *F_Size)(const USTR*);
typedef ULONG   (NTAPI *F_SizeA)(const ASTR*);
typedef LONG    (NTAPI *F_U2U8)(ASTR*, const USTR*, BOOLEAN);
typedef LONG    (NTAPI *F_U82U)(USTR*, const ASTR*, BOOLEAN);
typedef ULONG   (NTAPI *F_Rand)(ULONG*);
typedef LONG    (NTAPI *F_Guid)(const GUID*, USTR*);

static double freq;
static uint64_t sink;
static char whatsit[200];

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

static F_U2I      p_u2i;
static F_EqName   p_eqcomp, p_eqdom;
static F_Validate p_validate;
static F_Size     p_ansize, p_oemsize;
static F_SizeA    p_uni_from_ansi;
static F_U2U8     p_u2u8;
static F_U82U     p_u82u;
static F_Rand     p_rand, p_randex;
static F_Guid     p_guid;

static wchar_t wbuf[8192], wbuf2[8192];
static char    abuf[8192];
static USTR    us1, us2, usout;
static ASTR    as1, asout;
static ULONG   out32, seed = 12345;

static void op_u2i(void)      { sink += (unsigned)p_u2i(&us1, 10, &out32); }
static void op_eqcomp(void)   { sink += p_eqcomp(&us1, &us2); }
static void op_eqdom(void)    { sink += p_eqdom(&us1, &us2); }
static void op_validate(void) { sink += (unsigned)p_validate(0, &us1, &out32); }
static void op_ansize(void)   { sink += p_ansize(&us1); }
static void op_oemsize(void)  { sink += p_oemsize(&us1); }
static void op_unisize(void)  { sink += p_uni_from_ansi(&as1); }
static void op_u2u8(void)     { asout.Length = 0; sink += (unsigned)p_u2u8(&asout, &us1, FALSE); }
static void op_u82u(void)     { usout.Length = 0; sink += (unsigned)p_u82u(&usout, &as1, FALSE); }
static void op_rand(void)     { sink += p_rand(&seed); }
static void op_randex(void)   { sink += p_randex(&seed); }
static void op_guid(void)
{
    GUID g = { 0x12345678, 0x1234, 0x5678, { 1,2,3,4,5,6,7,8 } };
    usout.Length = 0; usout.MaximumLength = 128; usout.Buffer = wbuf2;
    sink += (unsigned)p_guid(&g, &usout);
}

#define ROW(fn, label, bytes, op, reps)                                                     \
    do {                                                                                    \
        if (!(fn)) { printf("  %-46s  NOT EXPORTED\n", label); break; }                      \
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

    p_u2i      = (F_U2I)     GetProcAddress(h, "RtlUnicodeStringToInteger");
    p_eqcomp   = (F_EqName)  GetProcAddress(h, "RtlEqualComputerName");
    p_eqdom    = (F_EqName)  GetProcAddress(h, "RtlEqualDomainName");
    p_validate = (F_Validate)GetProcAddress(h, "RtlValidateUnicodeString");
    p_ansize   = (F_Size)    GetProcAddress(h, "RtlUnicodeStringToAnsiSize");
    p_oemsize  = (F_Size)    GetProcAddress(h, "RtlUnicodeStringToOemSize");
    p_uni_from_ansi = (F_SizeA)GetProcAddress(h, "RtlAnsiStringToUnicodeSize");
    p_u2u8     = (F_U2U8)    GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    p_u82u     = (F_U82U)    GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    p_rand     = (F_Rand)    GetProcAddress(h, "RtlRandom");
    p_randex   = (F_Rand)    GetProcAddress(h, "RtlRandomEx");
    p_guid     = (F_Guid)    GetProcAddress(h, "RtlStringFromGUID");

    printf("ntdll Rtl* -- a FOURTH sweep: name comparisons, a parse, a validator, the size\n");
    printf("calculators and the UTF-8 string wrappers\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-40 and states what it did.\n\n");
    printf("  %-46s %9s %8s  %s\n", "export / subject", "ns", "ns/byte", "what it returned");

    /* ---- a 4000-character subject for everything length-driven ---- */
    for (i = 0; i < 4000; ++i) { wbuf[i] = (wchar_t)(L'a' + (i % 26)); wbuf2[i] = wbuf[i]; }
    for (i = 0; i < 4000; ++i) abuf[i] = (char)('a' + (i % 26));
    us1.Buffer = wbuf;  us1.Length = 8000; us1.MaximumLength = 8000;
    us2.Buffer = wbuf2; us2.Length = 8000; us2.MaximumLength = 8000;
    as1.Buffer = abuf;  as1.Length = 4000; as1.MaximumLength = 4000;
    asout.Buffer = (PSTR)malloc(20000);  asout.Length = 0; asout.MaximumLength = 20000;
    usout.Buffer = (PWSTR)malloc(40000); usout.Length = 0; usout.MaximumLength = 40000;

    /* THESE TWO REFUSE ANYTHING 64 CHARACTERS OR LONGER, and the first version of this file asked
       them about two identical 4000-character strings. They answered "not equal" in 7.9 ns and the
       row reported 0.001 ns/byte -- eight terabytes a second, which is the survey measuring a
       REFUSAL and calling it a comparison. That is exactly the mistake this file's own header
       warns about, and it was caught only because the row prints what it returned.

       On a legal subject they cost 559 ns and 539 ns to compare FIFTEEN characters, which is
       enormous -- and they are NOT CONVERTIBLE, which is why no change follows them. Their fold is
       not RtlUpcaseUnicodeChar: over a dense sweep of character pairs, 662 pairs compare EQUAL
       that the upcase table disagrees about (U+01C4 against U+01C5, U+0279 against U+02B4) and
       10401 pairs sharing an upcase compare NOT equal -- including control characters, which do
       not compare equal even to themselves. That is an OEM code-page conversion, and this project
       does not reproduce code-page tables. The same question ended lstrcmp_is_linguistic.c. */
    for (i = 0; i < 15; ++i) { wbuf[i] = (wchar_t)(L'a' + i); wbuf2[i] = wbuf[i]; }
    us1.Length = 30; us1.MaximumLength = 32;
    us2.Length = 30; us2.MaximumLength = 32;
    if (p_eqcomp) sprintf(whatsit, "equal=%d -- case-insensitive, but NOT via the upcase table",
                          p_eqcomp(&us1, &us2));
    ROW(p_eqcomp, "RtlEqualComputerName, 15 ch (a LEGAL size)", 30, op_eqcomp, 20000);
    if (p_eqdom)  sprintf(whatsit, "equal=%d -- and both refuse 64 characters or more",
                          p_eqdom(&us1, &us2));
    ROW(p_eqdom,  "RtlEqualDomainName, 15 ch (a LEGAL size)",   30, op_eqdom,  20000);

    /* back to the long subject for the rows that really are length-driven */
    for (i = 0; i < 4000; ++i) { wbuf[i] = (wchar_t)(L'a' + (i % 26)); wbuf2[i] = wbuf[i]; }
    us1.Length = 8000; us1.MaximumLength = 8000;
    us2.Length = 8000; us2.MaximumLength = 8000;

    if (p_validate) { out32 = 0; sprintf(whatsit, "status=%08lX", (unsigned long)p_validate(0, &us1, &out32)); }
    ROW(p_validate, "RtlValidateUnicodeString, 4000 ch",  8000, op_validate, 2000);

    if (p_ansize)  sprintf(whatsit, "ansi size=%lu", (unsigned long)p_ansize(&us1));
    ROW(p_ansize,  "RtlUnicodeStringToAnsiSize, 4000 ch", 8000, op_ansize,  5000);
    if (p_oemsize) sprintf(whatsit, "oem size=%lu", (unsigned long)p_oemsize(&us1));
    ROW(p_oemsize, "RtlUnicodeStringToOemSize, 4000 ch",  8000, op_oemsize, 5000);
    if (p_uni_from_ansi) sprintf(whatsit, "unicode size=%lu", (unsigned long)p_uni_from_ansi(&as1));
    ROW(p_uni_from_ansi, "RtlAnsiStringToUnicodeSize, 4000 B", 4000, op_unisize, 5000);

    if (p_u2u8) { asout.Length = 0; p_u2u8(&asout, &us1, FALSE);
                  sprintf(whatsit, "UTF-8 Length=%u", asout.Length); }
    ROW(p_u2u8, "RtlUnicodeStringToUTF8String, 4000 ch",  8000, op_u2u8, 2000);
    if (p_u82u) { usout.Length = 0; p_u82u(&usout, &as1, FALSE);
                  sprintf(whatsit, "UTF-16 Length=%u", usout.Length); }
    ROW(p_u82u, "RtlUTF8StringToUnicodeString, 4000 B",   4000, op_u82u, 2000);

    {
        static const wchar_t* NUM = L"4294967294";
        for (i = 0; NUM[i]; ++i) wbuf2[i] = NUM[i];
        us1.Buffer = wbuf2; us1.Length = (USHORT)(i * 2); us1.MaximumLength = us1.Length;
        out32 = 0; if (p_u2i) p_u2i(&us1, 10, &out32);
        sprintf(whatsit, "value=%lu from 10 characters", (unsigned long)out32);
        ROW(p_u2i, "RtlUnicodeStringToInteger, 10 digits", 20, op_u2i, 20000);
    }

    sprintf(whatsit, "a pseudo-random ULONG");
    ROW(p_rand,   "RtlRandom",                                0, op_rand,   50000);
    ROW(p_randex, "RtlRandomEx",                              0, op_randex, 50000);

    /* RtlStringFromGUID ALLOCATES its result: the buffer handed to it is not the one written,
       which is why an earlier version of this row printed stale contents from a previous test.
       That puts it outside what this project converts -- change 118 covers RtlStringFromGUIDEx,
       which writes into a caller's buffer -- and most of its 350 ns is the allocator. */
    if (p_guid) { op_guid(); sprintf(whatsit, "Length=%u, into a buffer IT allocated", usout.Length); }
    ROW(p_guid, "RtlStringFromGUID (it ALLOCATES)",            0, op_guid,   20000);

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
