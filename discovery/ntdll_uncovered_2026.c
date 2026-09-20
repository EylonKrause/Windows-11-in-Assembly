/* discovery/ntdll_uncovered_2026.c
 *
 * Where is the remaining headroom in ntdll's Rtl* surface?
 *
 * A coverage map over ntdll's export table says: 1038 Rtl* exports, of which 294 fall in the
 * string / memory / bit / number / time families this project targets, of which 204 are not covered
 * by any change. Most of those 204 are not candidates at all, hash tables, timers, memory
 * streams, environment blocks, security descriptors, but several are exactly the shape this
 * project is good at, and one group stands out:
 *
 *     The repository covers the bitmap readers and not one bitmap writer.
 *
 * Changes 255-262 cover RtlFindLongestRunClear, RtlAreBitsSet/Clear, RtlNumberOfSetBits,
 * RtlFindSetBits, RtlFindClearRuns, RtlFindNextForwardRunClear and RtlFindSetBitsAndClear. Every
 * one of them READS. RtlSetBits, RtlClearBits, RtlSetAllBits and RtlClearAllBits WRITE, they are
 * pure vectorisable memory work over the same structure, and nothing here has touched them.
 *
 * This sweep times that group and the other plausible uncovered candidates so a target is chosen on
 * a measurement rather than on a name. Nothing is asserted; every line prints what it measured.
 *
 * a number here is only a reason to look. Change 129 parked at 1.10x because ntdll's short-input
 * parse was already lean, and changes 005/006 parked for the same reason. The cost of a call that
 * is mostly call overhead cannot be optimised away by anyone.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } ASTR;
typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;

typedef VOID     (NTAPI *F_bits)(RBM*, ULONG, ULONG);
typedef VOID     (NTAPI *F_all)(RBM*);
typedef NTSTATUS (NTAPI *F_us2i)(USTR*, ULONG, PULONG);
typedef ULONG    (NTAPI *F_size1)(USTR*);
typedef ULONG    (NTAPI *F_asize1)(ASTR*);
typedef NTSTATUS (NTAPI *F_mb2usz)(PULONG, const char*, ULONG);
typedef NTSTATUS (NTAPI *F_u2mbsz)(PULONG, const wchar_t*, ULONG);
typedef WCHAR    (NTAPI *F_wch)(WCHAR);
typedef CHAR     (NTAPI *F_ach)(CHAR);
typedef VOID     (NTAPI *F_cpustr)(USTR*, USTR*);
typedef VOID     (NTAPI *F_cpastr)(ASTR*, const ASTR*);
typedef NTSTATUS (NTAPI *F_appstr)(ASTR*, const ASTR*);
typedef CCHAR    (NTAPI *F_bit64)(ULONGLONG);
typedef BOOLEAN  (NTAPI *F_valid)(ULONG, USTR*);
typedef VOID     (NTAPI *F_fill)(void*, SIZE_T, UCHAR);
typedef VOID     (NTAPI *F_zero)(void*, SIZE_T);
typedef VOID     (NTAPI *F_copy)(void*, const void*, SIZE_T);

static HMODULE hn;
static LARGE_INTEGER fq;
static void* R(const char* n) { void* p = (void*)GetProcAddress(hn, n); if (!p) printf("    (%s not exported)\n", n); return p; }

static ULONG bmbuf[4096];
static RBM   bm;
static char  a8[8192];
static wchar_t w16[4096];
static char  dst8[8192];
static wchar_t dw16[4096];

#define TIME(label, reps, stmt)                                                   \
    do {                                                                          \
        LARGE_INTEGER t0, t1; long i_; volatile long long sink_ = 0; (void)sink_; \
        for (i_ = 0; i_ < (reps) / 8; ++i_) { stmt; }                             \
        QueryPerformanceCounter(&t0);                                             \
        for (i_ = 0; i_ < (reps); ++i_) { stmt; }                                 \
        QueryPerformanceCounter(&t1);                                             \
        printf("    %-46s %9.2f ns\n", label,                                     \
               (double)(t1.QuadPart - t0.QuadPart) * 1e9 / fq.QuadPart / (reps)); \
    } while (0)

int main(void)
{
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&fq);
    hn = GetModuleHandleW(L"ntdll.dll");

    for (i = 0; i < 4096; ++i) bmbuf[i] = 0x5A5A5A5Aul;
    bm.SizeOfBitMap = 4096 * 32; bm.Buffer = bmbuf;
    for (i = 0; i < 8192; ++i) a8[i] = (char)('a' + (i % 26));
    a8[8191] = 0;
    for (i = 0; i < 4096; ++i) w16[i] = (wchar_t)(L'a' + (i % 26));
    w16[4095] = 0;

    printf("== THE BITMAP WRITERS -- the family this repository has never touched ==\n");
    printf("   (it covers eight bitmap READERS and no writer at all)\n");
    {
        F_bits set = (F_bits)R("RtlSetBits"), clr = (F_bits)R("RtlClearBits");
        F_all  sa  = (F_all) R("RtlSetAllBits"), ca = (F_all)R("RtlClearAllBits");
        if (set) {
            TIME("RtlSetBits    8 bits",        2000000, set(&bm, 1000, 8));
            TIME("RtlSetBits    64 bits",       2000000, set(&bm, 1000, 64));
            TIME("RtlSetBits    1024 bits",      500000, set(&bm, 1000, 1024));
            TIME("RtlSetBits    65536 bits",      50000, set(&bm, 1000, 65536));
            TIME("RtlSetBits    unaligned 999",  500000, set(&bm, 999, 1001));
        }
        if (clr) {
            TIME("RtlClearBits  8 bits",        2000000, clr(&bm, 1000, 8));
            TIME("RtlClearBits  1024 bits",      500000, clr(&bm, 1000, 1024));
            TIME("RtlClearBits  65536 bits",      50000, clr(&bm, 1000, 65536));
        }
        if (sa) TIME("RtlSetAllBits    131072 bits",   50000, sa(&bm));
        if (ca) TIME("RtlClearAllBits  131072 bits",   50000, ca(&bm));
    }

    printf("\n== the parser, the remaining member of the 278/279/280 family ==\n");
    {
        F_us2i f = (F_us2i)R("RtlUnicodeStringToInteger");
        if (f) {
            USTR u; ULONG v;
            static wchar_t s1[] = L"1234567890";
            static wchar_t s2[] = L"0xDEADBEEF";
            static wchar_t s3[] = L"42";
            u.Buffer = s1; u.Length = 20; u.MaximumLength = 22;
            TIME("RtlUnicodeStringToInteger \"1234567890\" b10", 2000000, f(&u, 10, &v));
            u.Buffer = s2; u.Length = 20; u.MaximumLength = 22;
            TIME("RtlUnicodeStringToInteger \"0xDEADBEEF\" b0",  2000000, f(&u, 0, &v));
            u.Buffer = s3; u.Length = 4; u.MaximumLength = 6;
            TIME("RtlUnicodeStringToInteger \"42\" b10",         2000000, f(&u, 10, &v));
        }
    }

    printf("\n== the SIZE calculators: pure scans, nothing but counting ==\n");
    {
        F_size1  u2a = (F_size1) R("RtlxUnicodeStringToAnsiSize");
        F_asize1 a2u = (F_asize1)R("RtlxAnsiStringToUnicodeSize");
        F_mb2usz mbs = (F_mb2usz)R("RtlMultiByteToUnicodeSize");
        F_u2mbsz ums = (F_u2mbsz)R("RtlUnicodeToMultiByteSize");
        USTR u; ASTR a; ULONG n;
        u.Buffer = w16; u.Length = 2048; u.MaximumLength = 2050;
        a.Buffer = a8;  a.Length = 2048; a.MaximumLength = 2050;
        if (u2a) TIME("RtlxUnicodeStringToAnsiSize  1024 wchar", 1000000, u2a(&u));
        if (a2u) TIME("RtlxAnsiStringToUnicodeSize  2048 char",  1000000, a2u(&a));
        if (mbs) TIME("RtlMultiByteToUnicodeSize    2048 char",  1000000, mbs(&n, a8, 2048));
        if (ums) TIME("RtlUnicodeToMultiByteSize    1024 wchar", 1000000, ums(&n, w16, 2048));
    }

    printf("\n== single-character maps ==\n");
    {
        F_wch up = (F_wch)R("RtlUpcaseUnicodeChar"), dn = (F_wch)R("RtlDowncaseUnicodeChar");
        F_ach uc = (F_ach)R("RtlUpperChar");
        if (up) TIME("RtlUpcaseUnicodeChar",   5000000, up(L'a'));
        if (dn) TIME("RtlDowncaseUnicodeChar", 5000000, dn(L'A'));
        if (uc) TIME("RtlUpperChar",           5000000, uc('a'));
    }

    printf("\n== string copies and appends ==\n");
    {
        F_cpustr cu = (F_cpustr)R("RtlCopyUnicodeString");
        F_cpastr ca = (F_cpastr)R("RtlCopyString");
        F_appstr ap = (F_appstr)R("RtlAppendStringToString");
        USTR s, d; ASTR sa2, da;
        s.Buffer = w16; s.Length = 2048; s.MaximumLength = 2050;
        d.Buffer = dw16; d.Length = 0; d.MaximumLength = 4096;
        sa2.Buffer = a8; sa2.Length = 2048; sa2.MaximumLength = 2050;
        da.Buffer = dst8; da.Length = 0; da.MaximumLength = 8192;
        if (cu) TIME("RtlCopyUnicodeString   1024 wchar", 1000000, cu(&d, &s));
        if (ca) TIME("RtlCopyString          2048 char",  1000000, ca(&da, &sa2));
        if (ap) TIME("RtlAppendStringToString 2048 char", 1000000,
                     (da.Length = 0, ap(&da, &sa2)));
    }

    printf("\n== trivial bit helpers, for completeness ==\n");
    {
        F_bit64 ls = (F_bit64)R("RtlFindLeastSignificantBit");
        F_bit64 ms = (F_bit64)R("RtlFindMostSignificantBit");
        if (ls) TIME("RtlFindLeastSignificantBit", 5000000, ls(0x0000100000000000ull));
        if (ms) TIME("RtlFindMostSignificantBit",  5000000, ms(0x0000100000000000ull));
    }

    printf("\n== validation ==\n");
    {
        F_valid v = (F_valid)R("RtlValidateUnicodeString");
        USTR u; u.Buffer = w16; u.Length = 2048; u.MaximumLength = 2050;
        if (v) TIME("RtlValidateUnicodeString 1024 wchar", 1000000, v(0, &u));
    }

    printf("\n== the memory primitives -- are they real code or forwarders? ==\n");
    {
        F_fill fl = (F_fill)R("RtlFillMemory");
        F_zero ze = (F_zero)R("RtlZeroMemory");
        F_copy cp = (F_copy)R("RtlCopyMemory");
        F_copy mv = (F_copy)R("RtlMoveMemory");
        if (fl) TIME("RtlFillMemory  4096", 1000000, fl(dst8, 4096, 0x5A));
        if (ze) TIME("RtlZeroMemory  4096", 1000000, ze(dst8, 4096));
        if (cp) TIME("RtlCopyMemory  4096", 1000000, cp(dst8, a8, 4096));
        if (mv) TIME("RtlMoveMemory  4096", 1000000, mv(dst8, a8, 4096));
    }
    return 0;
}
