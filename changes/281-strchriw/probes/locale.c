/* changes/281-strchriw/probes/locale.c
 *
 * THE DECIDING QUESTION: IS StrChrIW's FOLD LOCALE-DEPENDENT?
 *
 * probes/foldtable.c took the relation straight from the export and it is far richer than any of
 * the earlier hypotheses: 59321 distinct classes, and a LARGEST CLASS OF 3237. Classes like
 *
 *     rep 0041 <- 0041 0061 1D2C 1D43        A, a, MODIFIER CAPITAL A, MODIFIER SMALL A
 *     rep 004B <- 004B 006B 1D37 1D4F 212A   K, k, superscripts, KELVIN SIGN
 *
 * plus a single bucket of 3237 members, are the signature of CompareStringW with NORM_IGNORECASE
 * called once per character: the TERTIARY weight (case, superscript form, the Kelvin sign) is
 * ignored, the SECONDARY weight (accents) is not -- which is exactly why e-acute does not match
 * 'e' -- and every IGNORABLE code point has zero weight, so they all compare equal to one another.
 * That also explains 43 ns per character precisely: it is a full collation call per character.
 *
 * Change 277 faced the same fork and came out the other side: CharUpperBuffW LOOKED locale-aware
 * and was proved not to be, agreeing with the invariant, German AND Turkish locales, which is what
 * made that change writable. Change 276 parked because its cost really was a collation this
 * project does not own.
 *
 * So this asks the only question that decides which of those two this is. Turkish is the test that
 * matters -- dotted and dotless i are where a locale-aware casing rule shows itself -- and the
 * thread locale is the lever the caller actually has.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);
static F_chr chrI;

static int matches(WCHAR hay, WCHAR needle)
{
    wchar_t s[2]; s[0] = hay; s[1] = 0;
    return chrI(s, needle) != NULL;
}

struct pair { WCHAR a, b; const char* what; };
static const struct pair PAIRS[] = {
    { L'i', L'I',  "i vs I            (Turkish splits these)" },
    { 0x0131, L'I', "dotless i vs I    (Turkish unites these)" },
    { 0x0130, L'i', "I-with-dot vs i   (Turkish unites these)" },
    { 0x0131, L'i', "dotless i vs i" },
    { L'a', 0x1D2C, "a vs MODIFIER CAPITAL A" },
    { L'k', 0x212A, "k vs KELVIN SIGN" },
    { L'e', 0x00E9, "e vs e-acute      (secondary: must stay DIFFERENT)" },
    { L's', 0x00DF, "s vs sharp s" },
    { L'a', 0xFF41, "a vs fullwidth a" },
    { 0x200B, 0x200C, "two ignorable code points" },
};

static void sweep(const char* label)
{
    unsigned k;
    printf("  %-26s", label);
    for (k = 0; k < sizeof PAIRS / sizeof PAIRS[0]; ++k)
        printf(" %d", matches(PAIRS[k].a, PAIRS[k].b));
    printf("\n");
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    unsigned k;

    setvbuf(stdout, NULL, _IONBF, 0);
    chrI = (F_chr)GetProcAddress(hs, "StrChrIW");
    if (!chrI) { printf("resolve failed\n"); return 1; }

    printf("== does the thread locale change what StrChrIW considers equal? ==\n\n");
    for (k = 0; k < sizeof PAIRS / sizeof PAIRS[0]; ++k)
        printf("   column %2u : %s\n", k, PAIRS[k].what);
    printf("\n");

    sweep("as launched");
    SetThreadLocale(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT));
    sweep("en-US");
    SetThreadLocale(MAKELCID(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), SORT_DEFAULT));
    sweep("de-DE");
    SetThreadLocale(MAKELCID(MAKELANGID(LANG_TURKISH, SUBLANG_DEFAULT), SORT_DEFAULT));
    sweep("tr-TR  <-- the one that matters");
    SetThreadLocale(LOCALE_INVARIANT);
    sweep("invariant");
    SetThreadLocale(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT));
    sweep("back to en-US");

    printf("\n== and with the UI language overridden too ==\n");
    {
        ULONG n = 0, sz = 0;
        WCHAR langs[64];
        if (SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, L"tr-TR\0\0", &n)) {
            sweep("tr-TR preferred UI");
            SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, L"en-US\0\0", &n);
        } else {
            printf("   (could not set preferred UI languages)\n");
        }
        (void)langs; (void)sz;
    }

    printf("\n   identical rows in every locale  -> the fold is INVARIANT and this change can\n");
    printf("   reproduce it with a table; any row that differs -> it is a collation this project\n");
    printf("   does not own, and change 281 parks the way 276 did.\n");
    return 0;
}
