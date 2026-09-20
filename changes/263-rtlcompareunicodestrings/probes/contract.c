/* changes/263-rtlcompareunicodestrings/probes/contract.c
 *
 * What does ntdll!RtlCompareUnicodeStrings actually return?
 *
 *     LONG RtlCompareUnicodeStrings(PCWCH s1, SIZE_T len1, PCWCH s2, SIZE_T len2, BOOLEAN caseIns)
 *
 * discovery/rtl_cmpstrings_probe.c has already settled the two questions that decide whether this
 * is a target at all: it is a DISTINCT export from the landed RtlCompareUnicodeString (singular),
 * and its case-insensitive flag is exactly RtlUpcaseUnicodeChar -- 66462 equal pairs over a dense
 * sweep, with zero characters equal that the table disagrees about and zero different that it
 * agrees about. A flag that consulted a locale would have ended the change there, the way
 * discovery/lstrcmp_is_linguistic.c ended its target.
 *
 * What is left is the part that has to be reproduced BIT-exactly, and every question below exists
 * because getting it wrong would still produce a plausible-looking comparison:
 *
 *   * Is the return a sign or a value? a caller testing `< 0` cannot tell, but one storing the
 *     result can, and this project reproduces what the export returns rather than what callers
 *     are likely to look at.
 *   * What decides an unequal-length comparison -- the common prefix first, or the length? And
 *     what exactly is returned when one string is a prefix of the other?
 *   * Which character's difference is reported under the case-insensitive flag: the raw pair, or
 *     the upcased pair? They differ in sign for pairs like 'a' (0x61) against 'B' (0x42).
 *   * ZERO LENGTHS, and whether a NULL pointer is even looked at when the length is zero.
 *   * The lengths are in characters. That is not assumed: the first run of the discovery probe
 *     passed 2 for a single character and every character came back different from itself.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (NTAPI *F_Cmp)(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);
static F_Cmp cmp;

static void ask(const char* what, const wchar_t* a, SIZE_T la, const wchar_t* b, SIZE_T lb, int ci)
{
    LONG r = cmp(a, la, b, lb, (BOOLEAN)ci);
    printf("  %-56s %s  len %2Iu vs %2Iu -> %6ld\n", what, ci ? "CI" : "cs", la, lb, r);
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    cmp = (F_Cmp)GetProcAddress(h, "RtlCompareUnicodeStrings");
    if (!cmp) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== RtlCompareUnicodeStrings: the contract ==\n");
    printf("   the lengths are in CHARACTERS (verified: one character with len 1 equals itself)\n\n");

    printf("-- 1. is the return a SIGN, or the actual difference? --\n");
    ask("A vs A", L"A", 1, L"A", 1, 0);
    ask("A vs B  (difference 1)", L"A", 1, L"B", 1, 0);
    ask("A vs Z  (difference 25)", L"A", 1, L"Z", 1, 0);
    ask("A vs a  (difference 32)", L"A", 1, L"a", 1, 0);
    ask("Z vs A  (difference -25)", L"Z", 1, L"A", 1, 0);
    ask("U+0100 vs U+0000 (difference 256)", L"\x0100", 1, L"\x0000", 1, 0);
    ask("U+FFFF vs U+0000 (difference 65535)", L"\xFFFF", 1, L"\x0000", 1, 0);
    ask("U+0000 vs U+FFFF (difference -65535)", L"\x0000", 1, L"\xFFFF", 1, 0);

    printf("\n-- 2. the FIRST difference decides, and everything after it is irrelevant --\n");
    ask("abcZ vs abcA", L"abcZ", 4, L"abcA", 4, 0);
    ask("abcA vs abcZ", L"abcA", 4, L"abcZ", 4, 0);
    ask("aZZZ vs aAAA", L"aZZZ", 4, L"aAAA", 4, 0);

    printf("\n-- 3. UNEQUAL LENGTHS: the prefix first, or the length first? --\n");
    ask("abc vs abcd   (a prefix, shorter first)", L"abc", 3, L"abcd", 4, 0);
    ask("abcd vs abc   (a prefix, longer first)", L"abcd", 4, L"abc", 3, 0);
    ask("abc vs abd    (differs INSIDE the common part)", L"abc", 3, L"abd", 3, 0);
    ask("abz vs abcd   (longer, but differs early and HIGHER)", L"abz", 3, L"abcd", 4, 0);
    ask("aba vs abcd   (longer, but differs early and LOWER)", L"aba", 3, L"abcd", 4, 0);
    ask("abc vs abcdef (a prefix, two shorter)", L"abc", 3, L"abcdef", 6, 0);
    ask("abcdef vs abc (a prefix, two longer)", L"abcdef", 6, L"abc", 3, 0);

    printf("\n-- 4. ZERO LENGTHS, and whether the pointer is read at all --\n");
    ask("empty vs empty", L"", 0, L"", 0, 0);
    ask("empty vs abc", L"", 0, L"abc", 3, 0);
    ask("abc vs empty", L"abc", 3, L"", 0, 0);
    {
        LONG r = cmp(NULL, 0, NULL, 0, FALSE);
        printf("  %-56s %s  len %2d vs %2d -> %6ld\n", "NULL vs NULL, both lengths zero", "cs", 0, 0, r);
    }
    {
        LONG r = cmp(NULL, 0, L"abc", 3, FALSE);
        printf("  %-56s %s  len %2d vs %2d -> %6ld\n", "NULL (len 0) vs abc", "cs", 0, 3, r);
    }

    printf("\n-- 5. CASE-INSENSITIVE: whose difference is reported, the raw pair or the upcased? --\n");
    ask("a vs A", L"a", 1, L"A", 1, 1);
    ask("a vs B   (upcased A vs B = -1; raw a vs B = +31)", L"a", 1, L"B", 1, 1);
    ask("B vs a   (upcased B vs A = +1; raw B vs a = -31)", L"B", 1, L"a", 1, 1);
    ask("abc vs ABC", L"abc", 3, L"ABC", 3, 1);
    ask("abc vs ABD", L"abc", 3, L"ABD", 3, 1);
    ask("ABD vs abc", L"ABD", 3, L"abc", 3, 1);
    ask("the same pair, case-SENSITIVE", L"abc", 3, L"ABC", 3, 0);

    printf("\n-- 6. characters with no case, and the Latin-1 range --\n");
    ask("U+00E9 vs U+00C9 (e-acute vs E-acute)", L"\x00E9", 1, L"\x00C9", 1, 1);
    ask("... case-SENSITIVE", L"\x00E9", 1, L"\x00C9", 1, 0);
    ask("U+00DF vs U+00DF (sharp s, no upper in the table)", L"\x00DF", 1, L"\x00DF", 1, 1);
    ask("U+0131 vs U+0049 (dotless i vs I)", L"\x0131", 1, L"\x0049", 1, 1);
    ask("U+03C3 vs U+03A3 (Greek sigma)", L"\x03C3", 1, L"\x03A3", 1, 1);
    ask("U+FF41 vs U+FF21 (fullwidth a vs A)", L"\xFF41", 1, L"\xFF21", 1, 1);

    printf("\n-- 7. a long common prefix, so the answer comes from deep inside --\n");
    {
        static wchar_t a[4096], b[4096];
        int i;
        for (i = 0; i < 4096; ++i) { a[i] = L'x'; b[i] = L'x'; }
        ask("4096 identical", a, 4096, b, 4096, 0);
        b[4000] = L'y';
        ask("4096, differing at 4000", a, 4096, b, 4096, 0);
        b[4000] = L'x'; b[0] = L'y';
        ask("4096, differing at 0", a, 4096, b, 4096, 0);
    }
    return 0;
}
