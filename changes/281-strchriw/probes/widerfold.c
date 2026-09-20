/* changes/281-strchriw/probes/widerfold.c
 *
 * My own contract probe had the blind spot this project keeps finding.
 *
 * probes/contract.c concluded that StrChrIW's equality is exactly the ordinal upcase table, with
 * "0 disagreements over 3892 candidate pairs". Then the correctness corpus disagreed on four
 * needles out of 65535 -- U+1D2C, U+1D2E, U+1D43, U+1D47 -- where the live export finds a plain
 * 'a' or 'b' and the ordinal table says they are unrelated characters.
 *
 * The reason contract.c could not see it is the reason changes 097 and 100 shipped broken: THE
 * Corpus could not express the case. It built its candidate pairs from CharUpperW, CharLowerW,
 * RtlUpcaseUnicodeChar and RtlDowncaseUnicodeChar of each code unit -- so a pair that NONE of those
 * four functions relates, such as (U+1D2C modifier letter capital a, 'a'), was never asked about.
 * Zero disagreements over the pairs it could generate, and it generated the wrong pairs.
 *
 * U+1D2C is modifier letter capital a. a linguistic collation folds it onto 'a'; an ordinal upcase
 * does not. So the question this file has to settle is the one that decides whether this change can
 * exist at all:
 *
 *     Is StrChrIW's equality a linguistic fold?
 *
 * If it is, the 43 ns per character is a collation this project does not own, and change 281 parks
 * exactly as 274 and 276 did. If it is an ordinal fold with a small set of extra equivalences, the
 * change is still writable -- the extras just have to be in the table.
 *
 * So this asks, for a curated set of needles chosen to separate the two hypotheses, WHICH members
 * of a curated alphabet StrChrIW actually matches -- with no reference to any case function at all.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);
static F_chr chrI;
static WCHAR (NTAPI *rtlup)(WCHAR);

static int matches(WCHAR hay, WCHAR needle)
{
    wchar_t s[2];
    s[0] = hay; s[1] = 0;
    return chrI(s, needle) != NULL;
}

static void row(const char* what, WCHAR needle)
{
    static const WCHAR ALPHA[] = {
        L'a', L'A', L'e', L'E', L'b', L'B', L'o', L'O', L'n', L'N', L's', L'S',
        0x00E9, 0x00C9,        /* e-acute, E-acute */
        0x00DF,                /* sharp s */
        0x00F1, 0x00D1,        /* n-tilde */
        0x0430, 0x0410,        /* Cyrillic a, A */
        0x03B1, 0x0391,        /* Greek alpha */
        0xFF41, 0xFF21,        /* fullwidth a, A */
        0x1D2C, 0x1D43,        /* modifier letter capital A, modifier small a */
        0x0131, 0x0130         /* dotless i, I-with-dot */
    };
    unsigned k;
    printf("  %-34s U+%04X  matches:", what, needle);
    for (k = 0; k < sizeof ALPHA / sizeof ALPHA[0]; ++k)
        if (matches(ALPHA[k], needle)) printf(" %04X", ALPHA[k]);
    printf("\n     ordinal upcase would say:          ");
    for (k = 0; k < sizeof ALPHA / sizeof ALPHA[0]; ++k)
        if (rtlup(ALPHA[k]) == rtlup(needle)) printf(" %04X", ALPHA[k]);
    printf("\n");
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");

    setvbuf(stdout, NULL, _IONBF, 0);
    chrI  = (F_chr)GetProcAddress(hs, "StrChrIW");
    rtlup = (WCHAR (NTAPI *)(WCHAR))GetProcAddress(hn, "RtlUpcaseUnicodeChar");
    if (!chrI || !rtlup) { printf("resolve failed\n"); return 1; }

    printf("== WHAT DOES StrChrIW ACTUALLY MATCH? (no case function was used to choose these) ==\n\n");
    row("plain a",                        L'a');
    row("plain A",                        L'A');
    row("e-acute",                        0x00E9);
    row("E-acute",                        0x00C9);
    row("MODIFIER LETTER CAPITAL A",      0x1D2C);
    row("MODIFIER LETTER SMALL A",        0x1D43);
    row("MODIFIER LETTER CAPITAL B",      0x1D2E);
    row("Cyrillic small a",               0x0430);
    row("Greek small alpha",              0x03B1);
    row("fullwidth small a",              0xFF41);
    row("dotless i",                      0x0131);
    row("sharp s",                        0x00DF);
    row("n-tilde",                        0x00F1);

    printf("\n== THE DECIDING QUESTION ==\n");
    printf("   If this were a LINGUISTIC fold, an accented letter would match its base letter.\n");
    printf("   e-acute finds plain 'e'     : %s\n", matches(L'e', 0x00E9) ? "YES -> LINGUISTIC" : "no");
    printf("   plain 'e' finds e-acute     : %s\n", matches(0x00E9, L'e') ? "YES -> LINGUISTIC" : "no");
    printf("   n-tilde finds plain 'n'     : %s\n", matches(L'n', 0x00F1) ? "YES -> LINGUISTIC" : "no");
    printf("   fullwidth a finds plain 'a' : %s\n", matches(L'a', 0xFF41) ? "YES -> wide fold" : "no");
    printf("   sharp s finds \"ss\"          : %s\n",
           chrI(L"ss", 0x00DF) ? "YES -> expansion" : "no");
    printf("   MODIFIER CAPITAL A finds 'a': %s\n", matches(L'a', 0x1D2C) ? "YES" : "no");
    printf("   MODIFIER SMALL a finds 'a'  : %s\n", matches(L'a', 0x1D43) ? "YES" : "no");

    printf("\n== how many of the 65536 differ from the ordinal table, and where? ==\n");
    {
        /* For every code unit, ask whether it matches the 62 ASCII alphanumerics. Any match that
           the ordinal table does not predict is an extra equivalence, and this asks EVERY code
           unit against a fixed alphabet rather than against pairs derived from a case function. */
        unsigned c, k;
        long extra = 0, shown = 0;
        static const WCHAR AZ[] = {
            L'a',L'b',L'c',L'd',L'e',L'f',L'g',L'h',L'i',L'j',L'k',L'l',L'm',
            L'n',L'o',L'p',L'q',L'r',L's',L't',L'u',L'v',L'w',L'x',L'y',L'z',
            L'0',L'1',L'2',L'3',L'4',L'5',L'6',L'7',L'8',L'9' };
        for (c = 1; c <= 0xFFFF; ++c) {
            for (k = 0; k < sizeof AZ / sizeof AZ[0]; ++k) {
                int got = matches(AZ[k], (WCHAR)c);
                int want = (rtlup(AZ[k]) == rtlup((WCHAR)c));
                if (got != want) {
                    ++extra;
                    if (shown < 30) {
                        printf("   U+%04X vs '%c' : StrChrIW %d, ordinal %d\n",
                               c, (char)AZ[k], got, want);
                        ++shown;
                    }
                }
            }
        }
        printf("   %ld (code unit, ASCII alphanumeric) pairs where StrChrIW and the ordinal\n"
               "   upcase table DISAGREE\n", extra);
    }
    return 0;
}
