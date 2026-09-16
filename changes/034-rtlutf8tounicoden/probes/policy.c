/* changes/034-rtlutf8tounicoden/probes/policy.c
 *
 * HOW MANY UTF-16 UNITS DOES RtlUTF8ToUnicodeN PRODUCE FROM MALFORMED UTF-8?
 *
 * This is the rule the measuring mode needs, and it cannot be guessed. A first attempt -- one
 * U+FFFD per bad byte -- over-counted badly: for a 48-byte run of continuation bytes the live
 * export produced 78 bytes of UTF-16 where that rule said 80, and it got worse with longer runs.
 * The export is emitting FEWER replacements than there are bad bytes, which means it consumes a
 * maximal invalid subsequence per replacement rather than one byte per replacement.
 *
 * Getting this wrong is not a cosmetic error: change 268's allocating path asks for the size and
 * then converts into a buffer of exactly that size. A size one unit short truncates a caller's
 * string; one unit long is a wasted byte that the Length field then lies about.
 *
 * So the policy is read off the export directly, one hand-built sequence at a time, and only then
 * turned into a rule. Every line prints the input bytes and the number of UTF-16 units that came
 * back, so the table can be checked by eye rather than trusted.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG (NTAPI *F_ToUniN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);
static F_ToUniN nu;
static wchar_t out[64];

static void ask(const char* what, const unsigned char* b, int n)
{
    ULONG got = 0;
    LONG st;
    int i;
    st = nu(out, sizeof out, &got, (const char*)b, (ULONG)n);
    printf("  %-40s", what);
    for (i = 0; i < n; ++i) printf(" %02X", b[i]);
    for (; i < 6; ++i) printf("   ");
    printf("  -> %08lX  %lu units:", (unsigned long)st,
           (unsigned long)(got / 2));
    for (i = 0; i < (int)(got / 2) && i < 6; ++i) printf(" %04X", (unsigned)out[i]);
    printf("\n");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    nu = (F_ToUniN)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    if (!nu) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== what the shipped decoder does with malformed UTF-8 ==\n");
    printf("  (U+FFFD is the replacement character)\n\n");

    printf("-- stray continuation bytes --\n");
    { unsigned char b[] = { 0x80 };                         ask("one continuation", b, 1); }
    { unsigned char b[] = { 0x80, 0x80 };                   ask("two continuations", b, 2); }
    { unsigned char b[] = { 0x80, 0x80, 0x80 };             ask("three continuations", b, 3); }
    { unsigned char b[] = { 0x80, 0x80, 0x80, 0x80 };       ask("four continuations", b, 4); }
    { unsigned char b[] = { 'a', 0x80, 'b' };               ask("a continuation between letters", b, 3); }

    printf("\n-- truncated sequences --\n");
    { unsigned char b[] = { 0xC3 };                         ask("a 2-byte lead, nothing after", b, 1); }
    { unsigned char b[] = { 0xE2, 0x82 };                   ask("a 3-byte lead, one continuation", b, 2); }
    { unsigned char b[] = { 0xE2 };                         ask("a 3-byte lead alone", b, 1); }
    { unsigned char b[] = { 0xF0, 0x9F, 0x98 };             ask("a 4-byte lead, two continuations", b, 3); }
    { unsigned char b[] = { 0xF0, 0x9F };                   ask("a 4-byte lead, one continuation", b, 2); }
    { unsigned char b[] = { 0xF0 };                         ask("a 4-byte lead alone", b, 1); }

    printf("\n-- a lead byte interrupted by something that is not a continuation --\n");
    { unsigned char b[] = { 0xE2, 0x41 };                   ask("3-byte lead then 'A'", b, 2); }
    { unsigned char b[] = { 0xE2, 0x82, 0x41 };             ask("3-byte lead, cont, then 'A'", b, 3); }
    { unsigned char b[] = { 0xF0, 0x41 };                   ask("4-byte lead then 'A'", b, 2); }
    { unsigned char b[] = { 0xF0, 0x9F, 0x41 };             ask("4-byte lead, cont, then 'A'", b, 3); }
    { unsigned char b[] = { 0xC3, 0x41 };                   ask("2-byte lead then 'A'", b, 2); }

    printf("\n-- invalid lead bytes --\n");
    { unsigned char b[] = { 0xC0, 0xAF };                   ask("C0 AF, an overlong '/'", b, 2); }
    { unsigned char b[] = { 0xC1, 0xBF };                   ask("C1 BF, overlong", b, 2); }
    { unsigned char b[] = { 0xF5, 0x80, 0x80, 0x80 };       ask("F5, above U+10FFFF", b, 4); }
    { unsigned char b[] = { 0xFE };                         ask("FE, never valid", b, 1); }
    { unsigned char b[] = { 0xFF, 0xFF };                   ask("FF FF", b, 2); }

    printf("\n-- surrogates and out-of-range, encoded --\n");
    { unsigned char b[] = { 0xED, 0xA0, 0x80 };             ask("ED A0 80 = U+D800", b, 3); }
    { unsigned char b[] = { 0xE0, 0x80, 0xAF };             ask("E0 80 AF, overlong", b, 3); }
    { unsigned char b[] = { 0xF4, 0x90, 0x80, 0x80 };       ask("F4 90.., above U+10FFFF", b, 4); }

    printf("\n-- and the valid ones, for contrast --\n");
    { unsigned char b[] = { 'a' };                          ask("ASCII", b, 1); }
    { unsigned char b[] = { 0xC3, 0xA9 };                   ask("C3 A9 = U+00E9", b, 2); }
    { unsigned char b[] = { 0xE2, 0x82, 0xAC };             ask("E2 82 AC = U+20AC", b, 3); }
    { unsigned char b[] = { 0xF0, 0x9F, 0x98, 0x80 };       ask("F0 9F 98 80 = U+1F600, a PAIR", b, 4); }
    return 0;
}
