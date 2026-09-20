/* discovery/ntdll_tier3.c
 *
 * TIER 3 -- the uncovered ntdll Rtl* functions that the live desktop and startup processes actually
 * bind, ranked by fan-in and then timed.
 *
 * The fan-in numbers come from the two import sweeps in revalidation/*-imports-cache.json, so they
 * count DISTINCT LIVE MODULES that bind the name, not guesses:
 *
 *      RtlUpcaseUnicodeChar        32        RtlCopyUnicodeString        31
 *      RtlUnicodeStringToInteger   25        RtlMultiByteToUnicodeSize   12
 *      RtlInitializeBitMap          9        RtlSetBits / RtlClearBits    5 / 4
 *
 * Three of those are plausibly real work and the rest are probably finished code; the point of this
 * file is to stop guessing which is which. A function whose cost does not move with its input, and
 * whose absolute cost is already a handful of nanoseconds, is finished -- tier 2 ruled out
 * GetSystemTimeAsFileTime at 1.80 ns despite 561 modules binding it.
 *
 * RtlSetBits and RtlClearBits are included for a different reason: change 130 covers RtlSetBits and
 * is PARKED, and its Tiger Lake variant found that the shipped-vs-ours crossover is governed by
 * ERMS `rep stosb` rather than by vector stores. RtlClearBits is the same function with a zero fill
 * and is NOT covered at all. Timing them side by side says whether the complement is worth doing.
 *
 * BUILD
 *   . .\tools\vsenv.ps1
 *   cl /nologo /O2 discovery\ntdll_tier3.c /Fe:t3.exe ntdll.lib && .\t3.exe
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; char* Buffer; } ASTR;
typedef struct { ULONG SizeOfBitMap; ULONG* Buffer; } RTLBM;

typedef wchar_t (NTAPI *pfn_UpChar)(wchar_t);
typedef void    (NTAPI *pfn_CopyU)(USTR*, const USTR*);
typedef void    (NTAPI *pfn_CopyA)(ASTR*, const ASTR*);
typedef LONG    (NTAPI *pfn_U2I)(const USTR*, ULONG, ULONG*);
typedef LONG    (NTAPI *pfn_MBSize)(ULONG*, const char*, ULONG);
typedef void    (NTAPI *pfn_InitBM)(RTLBM*, ULONG*, ULONG);
typedef void    (NTAPI *pfn_SetBits)(RTLBM*, ULONG, ULONG);
typedef void    (NTAPI *pfn_AllBits)(RTLBM*);

static double qpc_freq;
static void timer_init(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpc_freq = (double)f.QuadPart; }
static double now_ns(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart * 1e9 / qpc_freq; }

#define TRIALS 25
#define REPS   2000
static volatile uint64_t sink;

typedef struct { const char* name; double ns; double per_byte; } row_t;
static row_t rows[80];
static int nrows;
static void row(const char* n, double ns, int bytes) {
    rows[nrows].name = n; rows[nrows].ns = ns;
    rows[nrows].per_byte = bytes > 0 ? ns / bytes : 0.0; ++nrows;
}
#define TIME_BLOCK(label, bytes, body)                                      \
    do { double best = 1e30;                                                \
         for (int t = 0; t < TRIALS; ++t) {                                 \
             double t0 = now_ns();                                          \
             for (int r = 0; r < REPS; ++r) { body; }                       \
             double dt = (now_ns() - t0) / REPS;                            \
             if (dt < best) best = dt;                                      \
         } row(label, best, (bytes)); } while (0)

static void* sym(const char* fn) {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    return h ? (void*)GetProcAddress(h, fn) : NULL;
}

int main(void) {
    timer_init();
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    pfn_UpChar pUp   = (pfn_UpChar)sym("RtlUpcaseUnicodeChar");
    pfn_UpChar pDown = (pfn_UpChar)sym("RtlDowncaseUnicodeChar");
    pfn_CopyU  pCopyU= (pfn_CopyU)sym("RtlCopyUnicodeString");
    pfn_CopyA  pCopyA= (pfn_CopyA)sym("RtlCopyString");
    pfn_U2I    pU2I  = (pfn_U2I)sym("RtlUnicodeStringToInteger");
    pfn_MBSize pMBS  = (pfn_MBSize)sym("RtlMultiByteToUnicodeSize");
    pfn_InitBM pInit = (pfn_InitBM)sym("RtlInitializeBitMap");
    pfn_SetBits pSet = (pfn_SetBits)sym("RtlSetBits");
    pfn_SetBits pClr = (pfn_SetBits)sym("RtlClearBits");
    pfn_AllBits pSetAll = (pfn_AllBits)sym("RtlSetAllBits");
    pfn_AllBits pClrAll = (pfn_AllBits)sym("RtlClearAllBits");

    /* ---- single-character case folding: table lookup or something more? ---- */
    if (pUp)   TIME_BLOCK("RtlUpcaseUnicodeChar (ASCII)      [fan-in 32]", 0, sink += pUp(L'a'));
    if (pUp)   TIME_BLOCK("RtlUpcaseUnicodeChar (Cyrillic)", 0, sink += pUp((wchar_t)0x0430));
    if (pUp)   TIME_BLOCK("RtlUpcaseUnicodeChar (CJK, no mapping)", 0, sink += pUp((wchar_t)0x4E00));
    if (pDown) TIME_BLOCK("RtlDowncaseUnicodeChar (ASCII)", 0, sink += pDown(L'A'));

    /* ---- bounded UNICODE_STRING copy: this one IS length-driven ---- */
    {
        static wchar_t src[8200], dst[8200];
        for (int i = 0; i < 8191; ++i) src[i] = (wchar_t)(L'a' + (i & 15));
        src[8191] = 0;
        static const int L[] = { 8, 64, 254, 1024, 4095 };
        static const char* N[] = { "RtlCopyUnicodeString 8   [fan-in 31]",
                                   "RtlCopyUnicodeString 64",
                                   "RtlCopyUnicodeString 254",
                                   "RtlCopyUnicodeString 1024",
                                   "RtlCopyUnicodeString 4095" };
        for (int k = 0; k < 5 && pCopyU; ++k) {
            USTR s, d;
            s.Length = (USHORT)(L[k] * 2); s.MaximumLength = (USHORT)(L[k] * 2); s.Buffer = src;
            d.Length = 0; d.MaximumLength = sizeof dst > 65535 ? 65534 : (USHORT)sizeof dst; d.Buffer = dst;
            TIME_BLOCK(N[k], L[k] * 2, { pCopyU(&d, &s); sink += d.Length; });
        }
        if (pCopyA) {
            static char sa[8200], da[8200];
            for (int i = 0; i < 8191; ++i) sa[i] = (char)('a' + (i & 15));
            ASTR s, d;
            s.Length = 254; s.MaximumLength = 254; s.Buffer = sa;
            d.Length = 0; d.MaximumLength = 8000; d.Buffer = da;
            TIME_BLOCK("RtlCopyString 254", 254, { pCopyA(&d, &s); sink += d.Length; });
        }
    }

    /* ---- UNICODE_STRING -> integer: the parse side ---- */
    if (pU2I) {
        static wchar_t d1[] = L"1", d9[] = L"123456789", dmax[] = L"4294967295";
        static wchar_t hx[] = L"0xDEADBEEF", oc[] = L"0o777", bn[] = L"0b101010";
        USTR u; ULONG v;
        u.Buffer = d1;   u.Length = 2;  u.MaximumLength = 4;
        TIME_BLOCK("RtlUnicodeStringToInteger \"1\"  [fan-in 25]", 0, { pU2I(&u, 10, &v); sink += v; });
        u.Buffer = d9;   u.Length = 18; u.MaximumLength = 20;
        TIME_BLOCK("RtlUnicodeStringToInteger 9 digits", 18, { pU2I(&u, 10, &v); sink += v; });
        u.Buffer = dmax; u.Length = 20; u.MaximumLength = 22;
        TIME_BLOCK("RtlUnicodeStringToInteger 10 digits (max)", 20, { pU2I(&u, 10, &v); sink += v; });
        u.Buffer = hx;   u.Length = 20; u.MaximumLength = 22;
        TIME_BLOCK("RtlUnicodeStringToInteger 0x prefix (base 0)", 0, { pU2I(&u, 0, &v); sink += v; });
        u.Buffer = oc;   u.Length = 10; u.MaximumLength = 12;
        TIME_BLOCK("RtlUnicodeStringToInteger 0o prefix (base 0)", 0, { pU2I(&u, 0, &v); sink += v; });
        u.Buffer = bn;   u.Length = 14; u.MaximumLength = 16;
        TIME_BLOCK("RtlUnicodeStringToInteger 0b prefix (base 0)", 0, { pU2I(&u, 0, &v); sink += v; });
    }

    /* ---- the counting pass that WideCharToMultiByte's inverse needs ---- */
    if (pMBS) {
        static char a[8200];
        for (int i = 0; i < 8191; ++i) a[i] = (char)('a' + (i & 15));
        ULONG n;
        TIME_BLOCK("RtlMultiByteToUnicodeSize 254  [fan-in 12]", 254, { pMBS(&n, a, 254); sink += n; });
        TIME_BLOCK("RtlMultiByteToUnicodeSize 4095", 4095, { pMBS(&n, a, 4095); sink += n; });
    }

    /* ---- the bitmap fills: RtlSetBits is change 130 (PARKED); RtlClearBits is UNCOVERED ---- */
    if (pInit && pSet && pClr) {
        static ULONG buf[8192];                    /* 256 Kbit */
        RTLBM bm;
        pInit(&bm, buf, 262144);
        TIME_BLOCK("RtlSetBits   40000 bits  [covered: change 130]", 5000, pSet(&bm, 0, 40000));
        TIME_BLOCK("RtlClearBits 40000 bits  [UNCOVERED, fan-in 4]", 5000, pClr(&bm, 0, 40000));
        TIME_BLOCK("RtlSetBits   262144 bits", 32768, pSet(&bm, 0, 262144));
        TIME_BLOCK("RtlClearBits 262144 bits", 32768, pClr(&bm, 0, 262144));
        if (pSetAll) TIME_BLOCK("RtlSetAllBits   262144 bits", 32768, pSetAll(&bm));
        if (pClrAll) TIME_BLOCK("RtlClearAllBits 262144 bits", 32768, pClrAll(&bm));
    }

    printf("\n== TIER 3: uncovered ntdll Rtl* that the live desktop/startup actually bind ==\n");
    printf("%-52s %12s %12s\n", "function (subject)", "ns/call", "ns/byte");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < nrows; ++i) {
        if (rows[i].per_byte > 0.0)
            printf("%-52s %12.2f %12.4f\n", rows[i].name, rows[i].ns, rows[i].per_byte);
        else
            printf("%-52s %12.2f %12s\n", rows[i].name, rows[i].ns, "flat");
    }
    printf("--------------------------------------------------------------------------------\n");
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
