/* discovery/rtl_integer_char.c
 *
 * Where the method still pays: Pure functions with no os-owned state in the way.
 *
 * Changes 274 and 276 were both parked for the same reason, and the reason is worth acting on rather
 * than repeating. In both the implementation was correct and enormously faster on the rows it could
 * affect, 274 was 4-5x above 256 characters, 276 was 24x on equal strings and 113x on identical
 * pointers, and in both the rows it could NOT affect turned out to be an operating-system call
 * this project does not own, with about a nanosecond of our code beside it:
 *
 *     274   SysAllocString    a 13.25 ns private allocator; a hand-made BSTR kills the process
 *     276   VarBstrCmp        a 33 ns collation; the shipped wrapper's whole overhead is 1.25 ns
 *
 * So this sweep deliberately avoids wrappers. It looks at exports that are pure functions of their
 * ARGUMENTS (no allocation, no locale, no per-thread state, nothing to delegate to) because
 * those are the ones where the whole measured cost is code this project can write. Change 067's
 * rewrite is the model: its `du` was a division per digit and replacing it was worth 3.4x on the
 * number and 1.42x -> 2.86x on the function.
 *
 * THREE FAMILIES, none of them touched by the thirty sweeps already here:
 *
 *   1. ntdll's INTEGER CONVERSIONS, RtlIntegerToUnicodeString, RtlUnicodeStringToInteger,
 *      RtlCharToInteger, RtlIntegerToChar, RtlLargeIntegerToChar. Number formatting and parsing in
 *      arbitrary bases, which is exactly what change 067 found running at a division per digit.
 *   2. user32's CHARACTER MAPPINGS, CharUpperW, CharLowerW, CharUpperBuffW, CharLowerBuffW,
 *      CharNextW, CharPrevW. This project already owns upcase tables built from the OS (changes 015
 *      and 210), so the question is only what the shipped ones cost per character.
 *   3. ntdll's BUFFER PREDICATES, RtlIsTextUnicode, which scans a buffer and returns statistics,
 *      and RtlCompareMemoryUlong, which is a search.
 *
 * Every row prints what it returned. discovery/ntdll_rtl_uncovered3.c timed a refusal as if it were
 * a comparison, 4000 identical characters answered in 7.9 ns, which is 0.001 ns per byte and
 * impossible, and only the returned value gave it away. A row that answers instantly because it
 * did nothing must be visible as such.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#pragma comment(lib, "user32.lib")

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } ASTR;

typedef LONG (NTAPI *F_I2US)(ULONG, ULONG, USTR*);
typedef LONG (NTAPI *F_US2I)(const USTR*, ULONG, ULONG*);
typedef LONG (NTAPI *F_C2I)(const char*, ULONG, ULONG*);
typedef LONG (NTAPI *F_I2C)(ULONG, ULONG, LONG, char*);
typedef LONG (NTAPI *F_LI2C)(LARGE_INTEGER*, ULONG, LONG, char*);   /* BY POINTER -- the
   first version of this sweep passed it by value and the run died at exit code 5 right here */
typedef BOOLEAN (NTAPI *F_ITU)(const void*, int, int*);
typedef SIZE_T (NTAPI *F_CMU)(const void*, SIZE_T, ULONG);

static double freq;
static unsigned long long sink;

static double timeit(void (*f)(void*), void* c, int inner_min)
{
    LARGE_INTEGER a, b;
    int inner = inner_min, t;
    double best = 1e300;
    for (;;) {
        double ns;
        QueryPerformanceCounter(&a);
        for (t = 0; t < inner; ++t) f(c);
        QueryPerformanceCounter(&b);
        ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq;
        if (ns >= 200000.0 || inner >= (1 << 22)) break;
        inner *= 4;
    }
    for (t = 0; t < 100; ++t) {
        LARGE_INTEGER x, y;
        int i;
        double ns;
        QueryPerformanceCounter(&x);
        for (i = 0; i < inner; ++i) f(c);
        QueryPerformanceCounter(&y);
        ns = (double)(y.QuadPart - x.QuadPart) * 1e9 / freq / inner;
        if (ns < best) best = ns;
    }
    return best;
}

/* ------------------------------------------------------------------ the subjects */
static F_I2US  p_i2us;
static F_US2I  p_us2i;
static F_C2I   p_c2i;
static F_I2C   p_i2c;
static F_LI2C  p_li2c;
static F_ITU   p_itu;
static F_CMU   p_cmu;

static wchar_t wbuf[256];
static char    abuf[256];
static USTR    us;
static ASTR    as;

static struct { ULONG v; ULONG base; } g_i2us;
static void op_i2us(void* c) { (void)c; us.Length = 0; us.MaximumLength = sizeof wbuf; us.Buffer = wbuf;
                               sink += (unsigned)p_i2us(g_i2us.v, g_i2us.base, &us); }
static struct { USTR s; ULONG base; } g_us2i;
static void op_us2i(void* c) { ULONG v = 0; (void)c; sink += (unsigned)p_us2i(&g_us2i.s, g_us2i.base, &v) + v; }
static struct { const char* s; ULONG base; } g_c2i;
static void op_c2i(void* c)  { ULONG v = 0; (void)c; sink += (unsigned)p_c2i(g_c2i.s, g_c2i.base, &v) + v; }
static struct { ULONG v; ULONG base; } g_i2c;
static void op_i2c(void* c)  { (void)c; sink += (unsigned)p_i2c(g_i2c.v, g_i2c.base, (LONG)sizeof abuf, abuf); }
static struct { LARGE_INTEGER v; ULONG base; } g_li2c;
static void op_li2c(void* c) { (void)c; sink += (unsigned)p_li2c(&g_li2c.v, g_li2c.base, (LONG)sizeof abuf, abuf); }

static wchar_t g_char[8200];
static int     g_charn;
static void op_upperbuf(void* c) { (void)c; sink += CharUpperBuffW(g_char, (DWORD)g_charn); }
static void op_lowerbuf(void* c) { (void)c; sink += CharLowerBuffW(g_char, (DWORD)g_charn); }
static void op_upper1(void* c)   { (void)c; sink += (UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)L'a'); }
static void op_next(void* c)     { (void)c; sink += (UINT_PTR)CharNextW(g_char); }

static void* g_buf;
static int   g_buflen;
static void op_itu(void* c)  { int f = -1; (void)c; sink += p_itu(g_buf, g_buflen, &f); }
static void op_cmu(void* c)  { (void)c; sink += p_cmu(g_buf, (SIZE_T)g_buflen, 0x41414141ul); }

int main(void)
{
    LARGE_INTEGER f;
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;

    p_i2us = (F_I2US)GetProcAddress(hn, "RtlIntegerToUnicodeString");
    p_us2i = (F_US2I)GetProcAddress(hn, "RtlUnicodeStringToInteger");
    p_c2i  = (F_C2I) GetProcAddress(hn, "RtlCharToInteger");
    p_i2c  = (F_I2C) GetProcAddress(hn, "RtlIntegerToChar");
    p_li2c = (F_LI2C)GetProcAddress(hn, "RtlLargeIntegerToChar");
    p_itu  = (F_ITU) GetProcAddress(hn, "RtlIsTextUnicode");
    p_cmu  = (F_CMU) GetProcAddress(hn, "RtlCompareMemoryUlong");

    printf("== what each one RETURNS, before anything is timed ==\n");
    if (p_i2us) {
        us.Length = 0; us.MaximumLength = sizeof wbuf; us.Buffer = wbuf;
        printf("   RtlIntegerToUnicodeString(3735928559, 16) -> %08lX  \"%.*ls\"\n",
               (unsigned long)p_i2us(3735928559ul, 16, &us), us.Length / 2, wbuf);
        us.Length = 0; us.MaximumLength = sizeof wbuf; us.Buffer = wbuf;
        printf("   RtlIntegerToUnicodeString(3735928559, 10) -> %08lX  \"%.*ls\"\n",
               (unsigned long)p_i2us(3735928559ul, 10, &us), us.Length / 2, wbuf);
    } else printf("   RtlIntegerToUnicodeString not found\n");
    if (p_us2i) {
        ULONG v = 0;
        USTR s;
        s.Buffer = (PWSTR)L"0xDEADBEEF"; s.Length = 20; s.MaximumLength = 20;
        printf("   RtlUnicodeStringToInteger(\"0xDEADBEEF\", 0) -> %08lX  value %lu\n",
               (unsigned long)p_us2i(&s, 0, &v), (unsigned long)v);
    } else printf("   RtlUnicodeStringToInteger not found\n");
    if (p_c2i) {
        ULONG v = 0;
        printf("   RtlCharToInteger(\"12345678\", 10) -> %08lX  value %lu\n",
               (unsigned long)p_c2i("12345678", 10, &v), (unsigned long)v);
    } else printf("   RtlCharToInteger not found\n");
    if (p_i2c) {
        abuf[0] = 0;
        printf("   RtlIntegerToChar(3735928559, 16) -> %08lX  \"%s\"\n",
               (unsigned long)p_i2c(3735928559ul, 16, (LONG)sizeof abuf, abuf), abuf);
    } else printf("   RtlIntegerToChar not found\n");
    if (p_li2c) {
        LARGE_INTEGER q; q.QuadPart = 1234567890123456789ll;
        abuf[0] = 0;
        printf("   RtlLargeIntegerToChar(1234567890123456789, 10) -> %08lX  \"%s\"\n",
               (unsigned long)p_li2c(&q, 10, (LONG)sizeof abuf, abuf), abuf);
    } else printf("   RtlLargeIntegerToChar not found\n");
    {
        wchar_t t[8] = L"hello!";
        int fl = -1;
        if (p_itu) printf("   RtlIsTextUnicode(\"hello!\", 12) -> %d, flags %08X\n",
                          p_itu(t, 12, &fl), fl);
        else printf("   RtlIsTextUnicode not found\n");
    }
    {
        static ULONG four[4] = { 0x41414141ul, 0x41414141ul, 0x41414141ul, 0x42424242ul };
        if (p_cmu) printf("   RtlCompareMemoryUlong(4 dwords, 0x41414141) -> %Iu bytes\n",
                          p_cmu(four, 16, 0x41414141ul));
        else printf("   RtlCompareMemoryUlong not found\n");
    }
    {
        wchar_t t[4] = L"a";
        printf("   CharUpperW(L'a') -> %04X   CharNextW(\"a\") steps %Id\n",
               (unsigned)(UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)L'a'),
               (INT_PTR)(CharNextW(t) - t));
    }

    printf("\n== the integer conversions: a pure function of its arguments, no state at all ==\n");
    printf("   %-42s %10s %10s\n", "export", "ns", "note");
    if (p_i2us) {
        g_i2us.v = 3735928559ul; g_i2us.base = 10;
        printf("   %-42s %10.2f %10s\n", "RtlIntegerToUnicodeString  base 10", timeit(op_i2us, 0, 64), "10 digits");
        g_i2us.base = 16;
        printf("   %-42s %10.2f %10s\n", "RtlIntegerToUnicodeString  base 16", timeit(op_i2us, 0, 64), "8 digits");
        g_i2us.base = 8;
        printf("   %-42s %10.2f %10s\n", "RtlIntegerToUnicodeString  base 8", timeit(op_i2us, 0, 64), "11 digits");
        g_i2us.base = 2;
        printf("   %-42s %10.2f %10s\n", "RtlIntegerToUnicodeString  base 2", timeit(op_i2us, 0, 64), "32 digits");
        g_i2us.v = 7; g_i2us.base = 10;
        printf("   %-42s %10.2f %10s\n", "RtlIntegerToUnicodeString  base 10", timeit(op_i2us, 0, 64), "1 digit");
    }
    if (p_i2c) {
        g_i2c.v = 3735928559ul; g_i2c.base = 10;
        printf("   %-42s %10.2f %10s\n", "RtlIntegerToChar           base 10", timeit(op_i2c, 0, 64), "10 digits");
        g_i2c.base = 16;
        printf("   %-42s %10.2f %10s\n", "RtlIntegerToChar           base 16", timeit(op_i2c, 0, 64), "8 digits");
    }
    if (p_li2c) {
        g_li2c.v.QuadPart = 1234567890123456789ll; g_li2c.base = 10;
        printf("   %-42s %10.2f %10s\n", "RtlLargeIntegerToChar      base 10", timeit(op_li2c, 0, 64), "19 digits");
        g_li2c.base = 16;
        printf("   %-42s %10.2f %10s\n", "RtlLargeIntegerToChar      base 16", timeit(op_li2c, 0, 64), "16 digits");
    }
    if (p_us2i) {
        g_us2i.s.Buffer = (PWSTR)L"3735928559"; g_us2i.s.Length = 20; g_us2i.s.MaximumLength = 20;
        g_us2i.base = 10;
        printf("   %-42s %10.2f %10s\n", "RtlUnicodeStringToInteger  base 10", timeit(op_us2i, 0, 64), "10 digits");
        g_us2i.s.Buffer = (PWSTR)L"0xDEADBEEF"; g_us2i.s.Length = 20; g_us2i.base = 0;
        printf("   %-42s %10.2f %10s\n", "RtlUnicodeStringToInteger  base 0", timeit(op_us2i, 0, 64), "0x prefix");
    }
    if (p_c2i) {
        g_c2i.s = "3735928559"; g_c2i.base = 10;
        printf("   %-42s %10.2f %10s\n", "RtlCharToInteger           base 10", timeit(op_c2i, 0, 64), "10 digits");
    }
    printf("   for scale: change 067's rewritten formatter does a 32-bit number in 3.76 ns,\n"
           "   and the division-per-digit version it replaced took 12.77\n");

    printf("\n== the character mappings, per character ==\n");
    {
        static const int LENS[] = { 1, 16, 256, 4000 };
        printf("   %-34s %10s %10s %12s\n", "export", "chars", "ns", "ns/char");
        for (i = 0; i < (int)(sizeof LENS / sizeof LENS[0]); ++i) {
            int k;
            double t;
            g_charn = LENS[i];
            for (k = 0; k < g_charn; ++k) g_char[k] = (wchar_t)(L'a' + (k % 26));
            g_char[g_charn] = 0;
            t = timeit(op_upperbuf, 0, 16);
            printf("   %-34s %10d %10.2f %12.4f\n", "user32!CharUpperBuffW", g_charn, t, t / g_charn);
            for (k = 0; k < g_charn; ++k) g_char[k] = (wchar_t)(L'A' + (k % 26));
            t = timeit(op_lowerbuf, 0, 16);
            printf("   %-34s %10d %10.2f %12.4f\n", "user32!CharLowerBuffW", g_charn, t, t / g_charn);
        }
        printf("   %-34s %10s %10.2f\n", "user32!CharUpperW (one character)", "1",
               timeit(op_upper1, 0, 256));
        printf("   %-34s %10s %10.2f\n", "user32!CharNextW", "1", timeit(op_next, 0, 256));
        printf("   for scale: change 015's RtlUpcaseUnicodeString runs at about 0.02 ns/char\n");
    }

    printf("\n== the buffer predicates ==\n");
    {
        static const int LENS[] = { 16, 256, 4000, 32000 };
        static char big[64000];
        for (i = 0; i < 64000; ++i) big[i] = (char)((i & 1) ? 0 : ('a' + (i / 2) % 26));
        g_buf = big;
        printf("   %-34s %10s %10s %12s\n", "export", "bytes", "ns", "ns/byte");
        for (i = 0; i < (int)(sizeof LENS / sizeof LENS[0]); ++i) {
            double t;
            g_buflen = LENS[i];
            if (p_itu) {
                t = timeit(op_itu, 0, 16);
                printf("   %-34s %10d %10.2f %12.4f\n", "ntdll!RtlIsTextUnicode", g_buflen, t,
                       t / g_buflen);
            }
            if (p_cmu) {
                t = timeit(op_cmu, 0, 16);
                printf("   %-34s %10d %10.2f %12.4f\n", "ntdll!RtlCompareMemoryUlong", g_buflen, t,
                       t / g_buflen);
            }
        }
        printf("   for scale: change 007's RtlCompareMemory runs at about 0.02 ns/byte\n");
    }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
