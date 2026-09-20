/* changes/268-rtlunicodestringtoutf8string/probes/contract.c
 *
 * What do RtlUnicodeStringToUTF8String and RtlUTF8StringToUnicodeString do, and where does their
 * TIME GO?
 *
 *     NTSTATUS RtlUnicodeStringToUTF8String(PUTF8_STRING dst, PCUNICODE_STRING src, BOOLEAN alloc)
 *     NTSTATUS RtlUTF8StringToUnicodeString(PUNICODE_STRING dst, PCUTF8_STRING src, BOOLEAN alloc)
 *
 * discovery/ntdll_rtl_uncovered3.c measured them at 0.097 and 0.213 ns/byte. This project already
 * converted the N-forms they wrap -- RtlUnicodeToUTF8N as change 016 at 2.82x and RtlUTF8ToUnicodeN
 * as change 034 at 3.12x -- so the interesting question is not "can the conversion be made faster"
 * but "how much of a wrapper call IS the conversion".
 *
 * That question decides whether there is a change here at all. If the wrapper is a size pass plus a
 * conversion pass, it reads the input TWICE and the ceiling is better than the N-form's own
 * speedup. If it is one pass plus a few stores, the ceiling is exactly the N-form's speedup and
 * nothing more. Section 3 measures the N-form directly on the same input so the two can be
 * subtracted rather than guessed at.
 *
 * And the rules have to be pinned either way:
 *   * with alloc = FALSE, what happens when the destination is exactly big enough, one byte short,
 *     and far too small?
 *   * is the result NUL-terminated, and does the terminator need room of its own?
 *   * what is Length set to, and MaximumLength?
 *   * is the destination left alone on failure?
 *   * what happens to an EMPTY source, and to invalid input -- an unpaired surrogate going out,
 *     and malformed UTF-8 coming in?
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG (NTAPI *F_U2U8)(U8STR*, const USTR*, BOOLEAN);
typedef LONG (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);
typedef LONG (NTAPI *F_ToUtf8N)(char*, ULONG, ULONG*, const wchar_t*, ULONG);
typedef LONG (NTAPI *F_ToUniN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);

static F_U2U8 u2u8;
static F_U82U u82u;

static wchar_t wsrc[8192];
static char    asrc[8192];
static char    adst[8192];
static wchar_t wdst[8192];

static void ask8(const char* what, const wchar_t* s, int wlen, USHORT maxlen)
{
    USTR in;
    U8STR out;
    LONG st;
    int i;
    memset(adst, '#', sizeof adst);
    in.Buffer = (PWSTR)s; in.Length = (USHORT)(wlen * 2); in.MaximumLength = in.Length;
    out.Buffer = adst; out.Length = 0xBEEF; out.MaximumLength = maxlen;
    st = u2u8(&out, &in, FALSE);
    printf("  %-44s max=%3u -> %08lX  Length=%-5u Max=%-5u [", what, maxlen,
           (unsigned long)st, out.Length, out.MaximumLength);
    for (i = 0; i < (maxlen < 16 ? maxlen + 3 : 16); ++i)
        printf("%c", adst[i] ? (adst[i] == '#' ? '#' : adst[i]) : '.');
    printf("]\n");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_ToUtf8N n_to8 = (F_ToUtf8N)GetProcAddress(h, "RtlUnicodeToUTF8N");
    F_ToUniN  n_toU = (F_ToUniN)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    int i;
    u2u8 = (F_U2U8)GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    u82u = (F_U82U)GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    if (!u2u8 || !u82u || !n_to8 || !n_toU) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== RtlUnicodeStringToUTF8String: the contract ==\n");
    printf("   '#' is untouched poison, '.' is a NUL that was written\n\n");

    printf("-- 1. the destination size boundary, converting \"abc\" (3 bytes of UTF-8) --\n");
    ask8("dest max 2 (too small)",              L"abc", 3, 2);
    ask8("dest max 3 (exactly the bytes)",      L"abc", 3, 3);
    ask8("dest max 4 (bytes plus one)",         L"abc", 3, 4);
    ask8("dest max 8 (plenty)",                 L"abc", 3, 8);
    ask8("an EMPTY source, dest max 8",         L"",    0, 8);
    ask8("an empty source, dest max 0",         L"",    0, 0);

    printf("\n-- 2. multi-byte output --\n");
    {
        static const wchar_t E[2] = { 0x00E9, 0 };     /* two UTF-8 bytes */
        static const wchar_t EU[2] = { 0x20AC, 0 };    /* three */
        ask8("U+00E9 (2 UTF-8 bytes), dest max 1", E, 1, 1);
        ask8("U+00E9, dest max 2", E, 1, 2);
        ask8("U+20AC (3 UTF-8 bytes), dest max 2", EU, 1, 2);
        ask8("U+20AC, dest max 3", EU, 1, 3);
    }

    printf("\n-- 3. an UNPAIRED SURROGATE, which has no valid UTF-8 form --\n");
    {
        static const wchar_t LONE[2] = { 0xD800, 0 };
        ask8("a lone high surrogate, dest max 8", LONE, 1, 8);
    }

    printf("\n-- 4. WHERE THE TIME GOES: the wrapper against the N-form it wraps --\n");
    {
        LARGE_INTEGER f, t0, t1;
        double wrap, nform;
        USTR in;
        U8STR out;
        ULONG produced = 0;
        volatile LONG sink = 0;
        QueryPerformanceFrequency(&f);
        for (i = 0; i < 4000; ++i) wsrc[i] = (wchar_t)(L'a' + (i % 26));
        in.Buffer = wsrc; in.Length = 8000; in.MaximumLength = 8000;
        out.Buffer = adst; out.MaximumLength = 8192;

        QueryPerformanceCounter(&t0);
        for (i = 0; i < 20000; ++i) { out.Length = 0; sink += u2u8(&out, &in, FALSE); }
        QueryPerformanceCounter(&t1);
        wrap = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / 20000.0;

        QueryPerformanceCounter(&t0);
        for (i = 0; i < 20000; ++i) sink += n_to8(adst, 8192, &produced, wsrc, 8000);
        QueryPerformanceCounter(&t1);
        nform = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / 20000.0;

        printf("   RtlUnicodeStringToUTF8String, 4000 chars : %8.2f ns\n", wrap);
        printf("   RtlUnicodeToUTF8N alone, the same input  : %8.2f ns  (produced %lu bytes)\n",
               nform, (unsigned long)produced);
        printf("   the wrapper costs %8.2f ns on top, which is %.0f%% of the call\n",
               wrap - nform, 100.0 * (wrap - nform) / wrap);
        printf("   %s\n", (wrap > nform * 1.6)
               ? "   -- that is far more than a few stores: it is reading the input TWICE,\n"
                 "      once to size the output and once to produce it"
               : "   -- so the wrapper is thin, and its ceiling is the N-form's own speedup");

        /* the same question for the other direction */
        for (i = 0; i < 4000; ++i) asrc[i] = (char)('a' + (i % 26));
        {
            U8STR in8;
            USTR outw;
            in8.Buffer = asrc; in8.Length = 4000; in8.MaximumLength = 4000;
            outw.Buffer = wdst; outw.MaximumLength = 16384;
            QueryPerformanceCounter(&t0);
            for (i = 0; i < 20000; ++i) { outw.Length = 0; sink += u82u(&outw, &in8, FALSE); }
            QueryPerformanceCounter(&t1);
            wrap = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / 20000.0;
            QueryPerformanceCounter(&t0);
            for (i = 0; i < 20000; ++i) sink += n_toU(wdst, 16384, &produced, asrc, 4000);
            QueryPerformanceCounter(&t1);
            nform = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / 20000.0;
            printf("\n   RtlUTF8StringToUnicodeString, 4000 bytes : %8.2f ns\n", wrap);
            printf("   RtlUTF8ToUnicodeN alone, the same input  : %8.2f ns  (produced %lu bytes)\n",
                   nform, (unsigned long)produced);
            printf("   the wrapper costs %8.2f ns on top, which is %.0f%% of the call\n",
                   wrap - nform, 100.0 * (wrap - nform) / wrap);
        }
        printf("sink=%ld\n", sink);
    }
    return 0;
}
