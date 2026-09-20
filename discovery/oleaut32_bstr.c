/* discovery/oleaut32_bstr.c
 *
 * oleaut32 is the last System32 DLL in this project's default set with ZERO conversions: 417
 * exports, none replaced, and two passing mentions in the whole of discovery/. Its shaped-candidate
 * list is 65 names. This measures the ones that could plausibly be byte loops and asks, for each,
 * the only question that decides whether a target exists: Is the cost the loop, or something no
 * Assembly can remove?
 *
 * For oleaut32 there are two such somethings, and almost every candidate has one of them:
 *
 *   THE ALLOCATOR. A BSTR is a length-prefixed heap block. SysAllocString, SysAllocStringLen,
 *   VarBstrCat and every VarBstrFrom* returns one, so each call is an allocation plus a copy. This
 *   file separates them: each allocating row is measured against SysAllocStringLen(NULL, n), which
 *   performs the SAME allocation and no conversion at all. The difference is the loop, and the loop
 *   is all that could ever be replaced. A row whose total is 95% allocator is not a target no
 *   matter how big the total looks.
 *
 *   THE LOCALE. Every VarXxxFromStr takes an LCID. This repository has ruled out lstrcmpA/iA,
 *   StrCmpNW/NIW and StrChrIW for exactly that reason -- the cost is the locale object, not the
 *   loop, and a baked-in table cannot be honest across locales. The rows here are timed at
 *   LOCALE_INVARIANT, which is the best case for us, so a row that is still slow there is slow
 *   because of the machinery rather than the data.
 *
 * What is deliberately not here. The SafeArray family (a descriptor data structure, not a byte
 * loop), the VarFormat family and VarTokenizeFormatString (a grammar), the TypeLib and IDispatch
 * families (registry and COM), and the marshalling entry points.
 *
 * METHOD, and the mistakes this file is written to avoid:
 *   * Run it on an idle machine. Every row is a min-of-N, robust to a slow sample, not to load.
 *   * Every row prints what it actually did -- the returned value or length. a survey row whose
 *     subject does not do the work its label claims is this project's most expensive recurring
 *     mistake, and it is invisible until the row is made to state itself.
 *   * Every BSTR is freed. a survey that leaks a BSTR per iteration measures the allocator warming
 *     up, then measures it degrading, and the min-of-N hides which.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

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

/* ---- subjects ------------------------------------------------------------------------------ */
#define NW 512
static WCHAR  src[NW + 8];
static WCHAR  numstr[64];
static BSTR   bs_a, bs_b;
static int    g_len = 64;          /* the length the length-parameterised rows use */

static LONG   g_i4;
static LONGLONG g_i8;
static double g_r8;

/* --- the allocator baseline: the same allocation, no conversion at all --- */
static void op_alloc_only(void)   { BSTR b = SysAllocStringLen(NULL, (UINT)g_len); sink += (size_t)b; SysFreeString(b); }
static void op_alloc_copy(void)   { BSTR b = SysAllocStringLen(src, (UINT)g_len);  sink += (size_t)b; SysFreeString(b); }
static void op_alloc_strlen(void) { BSTR b = SysAllocString(src);                  sink += (size_t)b; SysFreeString(b); }

/* --- the O(1) accessors --- */
static void op_strlen(void)     { sink += SysStringLen(bs_a); }
static void op_bytelen(void)    { sink += SysStringByteLen(bs_a); }

/* --- concatenation: one allocation, two copies --- */
static void op_bstrcat(void)    { BSTR o = NULL; VarBstrCat(bs_a, bs_b, &o); sink += (size_t)o; SysFreeString(o); }

/* --- integer and float formatting, each an allocation plus a digit loop --- */
static void op_from_i4(void)    { BSTR o = NULL; VarBstrFromI4 (g_i4, LOCALE_INVARIANT, 0, &o); sink += (size_t)o; SysFreeString(o); }
static void op_from_i8(void)    { BSTR o = NULL; VarBstrFromI8 (g_i8, LOCALE_INVARIANT, 0, &o); sink += (size_t)o; SysFreeString(o); }
static void op_from_r8(void)    { BSTR o = NULL; VarBstrFromR8 (g_r8, LOCALE_INVARIANT, 0, &o); sink += (size_t)o; SysFreeString(o); }

/* --- parsing, all locale-parameterised --- */
static void op_to_i4(void)      { LONG v = 0;     VarI4FromStr (numstr, LOCALE_INVARIANT, 0, &v);  sink += (unsigned)v; }
static void op_to_ui4(void)     { ULONG v = 0;    VarUI4FromStr(numstr, LOCALE_INVARIANT, 0, &v);  sink += v; }
static void op_to_i8(void)      { LONGLONG v = 0; VarI8FromStr (numstr, LOCALE_INVARIANT, 0, &v);  sink += (unsigned long long)v; }
static void op_to_r8(void)      { double v = 0;   VarR8FromStr (numstr, LOCALE_INVARIANT, 0, &v);  sink += (unsigned long long)v; }

/* --- comparison, locale-parameterised --- */
static void op_bstrcmp(void)    { sink += (unsigned)VarBstrCmp(bs_a, bs_b, LOCALE_INVARIANT, 0); }
static void op_bstrcmp_bin(void){ sink += (unsigned)VarBstrCmp(bs_a, bs_b, LOCALE_INVARIANT, NORM_IGNORECASE); }

/* --- a pure hash, the one candidate with no allocator and no obvious locale cost --- */
static void op_lhash(void)      { sink += LHashValOfNameSys(SYS_WIN64, LOCALE_INVARIANT, src); }

/* --- the reference points this repository already owns, for scale --- */
static void op_wcslen(void)     { sink += wcslen(src); }
static void op_memcpy(void)     { static WCHAR d[NW + 8]; memcpy(d, src, (size_t)g_len * 2); sink += (size_t)d; }

static void row(const char* name, void (*op)(void), int perbyte, const char* did)
{
    double ns = bestns(op, 2000, 40);
    printf("  %-34s %9.2f ns", name, ns);
    if (perbyte) printf("   %7.3f ns/char", ns / (double)g_len);
    else         printf("                  ");
    printf("   %s\n", did);
}

int main(void)
{
    int i;
    char didbuf[160];

    for (i = 0; i < NW; ++i) src[i] = (WCHAR)(L'a' + (i % 26));
    src[NW] = 0;
    wcscpy_s(numstr, 64, L"1234567");
    g_i4 = 1234567;  g_i8 = 1234567890123LL;  g_r8 = 1234.5678;

    printf("oleaut32 BSTR and VARIANT conversion survey\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-40 and states what it actually did.\n\n");

    for (g_len = 16; g_len <= 256; g_len *= 4) {
        bs_a = SysAllocStringLen(src, (UINT)g_len);
        bs_b = SysAllocStringLen(src, (UINT)g_len);
        if (!bs_a || !bs_b) { printf("allocation failed\n"); return 2; }
        /* make the two differ in the last character, so a comparison cannot exit early */
        bs_b[g_len - 1] = L'Z';

        printf("---- length %d characters (%d bytes) ----\n", g_len, g_len * 2);

        sprintf_s(didbuf, sizeof didbuf, "the allocator ALONE -- no conversion; every row below pays this");
        row("SysAllocStringLen(NULL,n)", op_alloc_only, 1, didbuf);

        { BSTR b = SysAllocStringLen(src, (UINT)g_len);
          sprintf_s(didbuf, sizeof didbuf, "len=%u, first=%04X", SysStringLen(b), b[0]); SysFreeString(b); }
        row("SysAllocStringLen(src,n)", op_alloc_copy, 1, didbuf);

        { BSTR b = SysAllocString(src);
          sprintf_s(didbuf, sizeof didbuf, "len=%u (a strlen over %d chars, then alloc+copy)", SysStringLen(b), NW); SysFreeString(b); }
        row("SysAllocString(src)", op_alloc_strlen, 0, didbuf);

        sprintf_s(didbuf, sizeof didbuf, "returned %u", SysStringLen(bs_a));
        row("SysStringLen", op_strlen, 0, didbuf);
        sprintf_s(didbuf, sizeof didbuf, "returned %u", SysStringByteLen(bs_a));
        row("SysStringByteLen", op_bytelen, 0, didbuf);

        { BSTR o = NULL; VarBstrCat(bs_a, bs_b, &o);
          sprintf_s(didbuf, sizeof didbuf, "result len=%u (one alloc + two copies)", o ? SysStringLen(o) : 0);
          SysFreeString(o); }
        row("VarBstrCat", op_bstrcat, 0, didbuf);

        { HRESULT hr = VarBstrCmp(bs_a, bs_b, LOCALE_INVARIANT, 0);
          sprintf_s(didbuf, sizeof didbuf, "returned %ld (0=LT 1=EQ 2=GT), differ at the LAST char", (long)hr); }
        row("VarBstrCmp (ordinal-ish)", op_bstrcmp, 1, didbuf);
        { HRESULT hr = VarBstrCmp(bs_a, bs_b, LOCALE_INVARIANT, NORM_IGNORECASE);
          sprintf_s(didbuf, sizeof didbuf, "returned %ld, NORM_IGNORECASE", (long)hr); }
        row("VarBstrCmp (NORM_IGNORECASE)", op_bstrcmp_bin, 1, didbuf);

        sprintf_s(didbuf, sizeof didbuf, "hash of %d chars", g_len);
        row("LHashValOfNameSys", op_lhash, 1, didbuf);

        sprintf_s(didbuf, sizeof didbuf, "for scale: this repository's own territory");
        row("wcslen (reference point)", op_wcslen, 0, didbuf);
        sprintf_s(didbuf, sizeof didbuf, "for scale: %d bytes", g_len * 2);
        row("memcpy (reference point)", op_memcpy, 1, didbuf);

        SysFreeString(bs_a); SysFreeString(bs_b);
        printf("\n");
    }

    printf("---- the scalar conversions, which do not scale with a length ----\n");
    g_len = 1;
    { BSTR o = NULL; VarBstrFromI4(g_i4, LOCALE_INVARIANT, 0, &o);
      sprintf_s(didbuf, sizeof didbuf, "\"%ls\" (%u chars)", o ? o : L"", o ? SysStringLen(o) : 0); SysFreeString(o); }
    row("VarBstrFromI4", op_from_i4, 0, didbuf);
    { BSTR o = NULL; VarBstrFromI8(g_i8, LOCALE_INVARIANT, 0, &o);
      sprintf_s(didbuf, sizeof didbuf, "\"%ls\" (%u chars)", o ? o : L"", o ? SysStringLen(o) : 0); SysFreeString(o); }
    row("VarBstrFromI8", op_from_i8, 0, didbuf);
    { BSTR o = NULL; VarBstrFromR8(g_r8, LOCALE_INVARIANT, 0, &o);
      sprintf_s(didbuf, sizeof didbuf, "\"%ls\" (%u chars)", o ? o : L"", o ? SysStringLen(o) : 0); SysFreeString(o); }
    row("VarBstrFromR8", op_from_r8, 0, didbuf);

    { LONG v = 0; HRESULT hr = VarI4FromStr(numstr, LOCALE_INVARIANT, 0, &v);
      sprintf_s(didbuf, sizeof didbuf, "\"%ls\" -> %ld (hr=%08lX)", numstr, (long)v, (unsigned long)hr); }
    row("VarI4FromStr", op_to_i4, 0, didbuf);
    { ULONG v = 0; HRESULT hr = VarUI4FromStr(numstr, LOCALE_INVARIANT, 0, &v);
      sprintf_s(didbuf, sizeof didbuf, "\"%ls\" -> %lu (hr=%08lX)", numstr, v, (unsigned long)hr); }
    row("VarUI4FromStr", op_to_ui4, 0, didbuf);
    { LONGLONG v = 0; HRESULT hr = VarI8FromStr(numstr, LOCALE_INVARIANT, 0, &v);
      sprintf_s(didbuf, sizeof didbuf, "\"%ls\" -> %lld (hr=%08lX)", numstr, v, (unsigned long)hr); }
    row("VarI8FromStr", op_to_i8, 0, didbuf);
    { double v = 0; HRESULT hr = VarR8FromStr(numstr, LOCALE_INVARIANT, 0, &v);
      sprintf_s(didbuf, sizeof didbuf, "\"%ls\" -> %.4f (hr=%08lX)", numstr, v, (unsigned long)hr); }
    row("VarR8FromStr", op_to_r8, 0, didbuf);

    printf("\nHOW TO READ THIS. Subtract the SysAllocStringLen(NULL,n) row from every allocating row:\n"
           "what is left is the only part any assembly could replace. A conversion whose remainder is\n"
           "a small fraction of its total is not a target, however large the total looks -- the same\n"
           "conclusion the heap-entry-point filter in tools/uncovered-exports.py encodes up front.\n"
           "For the VarXxxFromStr rows, compare against this repository's own parsers: change 295\n"
           "(RtlUnicodeStringToInteger) and changes 110-113 (the strtoX family) run in single-digit\n"
           "nanoseconds, so anything here in the hundreds is spending it on the locale, not the loop.\n");
    sink += (unsigned)g_i4;
    return 0;
}
