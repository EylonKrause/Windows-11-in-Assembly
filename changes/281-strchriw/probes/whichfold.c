/* changes/281-strchriw/probes/whichfold.c
 *
 * WHICH WINDOWS FUNCTION PRODUCES StrChrIW's FOLD?
 *
 * probes/widerfold.c settled what it is NOT. It is not linguistic: e-acute does not find 'e',
 * n-tilde does not find 'n', fullwidth 'a' does not find 'a', and sharp s does not expand to "ss".
 * But it is wider than RtlUpcaseUnicodeChar, which is what probes/contract.c wrongly concluded --
 * the modifier and superscript letters fold onto their base letters:
 *
 *     U+01BB, U+01BC, U+01BD  ->  '2', '5', '5'
 *     U+02B0, U+02B2, U+02B3, U+02B7, U+02B8, U+02E1..U+02E3  ->  h, j, r, w, y, l, s, x
 *     U+1D2C..U+1D43 and beyond  ->  A, B, D, E, G, H, I, J, K, a, ...
 *
 * That is an ORDINAL table, just not ntdll's. This project's rule is that a table is built by
 * asking the function that owns it, never transcribed -- change 277's tables.c, change 269's
 * aliases, change 210's upcase table. So before any table is written down, the question is which
 * Windows function already computes this mapping, so that it can be ASKED for all 65536 code units
 * the way change 277 asks CharUpperBuffW.
 *
 * The candidates, and what each would mean:
 *
 *   LCMapStringW(LCMAP_UPPERCASE)                  the NLS uppercase table, which is NOT the same
 *                                                  object as ntdll's RTL table
 *   LCMapStringW(LCMAP_UPPERCASE|LCMAP_LINGUISTIC) the locale-aware variant -- if this is the one,
 *                                                  the change parks, because it is locale-dependent
 *   FoldStringW(MAP_FOLDCZONE)                     compatibility-zone folding, which is exactly the
 *                                                  shape of the observed extras
 *   FoldStringW(MAP_COMPOSITE / MAP_PRECOMPOSED)   normalisation, which would also fold accents --
 *                                                  and accents are NOT folded, so this is unlikely
 *
 * Each candidate is asked about every code unit where StrChrIW is known to disagree with ntdll, and
 * then, if one matches on all of them, about ALL 65536 to be sure.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);
static F_chr chrI;
static WCHAR (NTAPI *rtlup)(WCHAR);

static int matches(WCHAR hay, WCHAR needle)
{
    wchar_t s[2]; s[0] = hay; s[1] = 0;
    return chrI(s, needle) != NULL;
}

static WCHAR lcmap(WCHAR c, DWORD flags, LCID loc)
{
    wchar_t in[2], out[8];
    int n;
    in[0] = c; in[1] = 0;
    n = LCMapStringW(loc, flags, in, 1, out, 8);
    return (n == 1) ? out[0] : c;          /* an expansion is not a single code unit */
}

static WCHAR foldstr(WCHAR c, DWORD flags)
{
    wchar_t in[2], out[8];
    int n;
    in[0] = c; in[1] = 0;
    n = FoldStringW(flags, in, 1, out, 8);
    return (n == 1) ? out[0] : c;
}

/* the reference relation, straight from the export: do a and b match? */
static int same(WCHAR a, WCHAR b) { return matches(a, b); }

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    unsigned c;
    long bad_inv = 0, bad_ling = 0, bad_user = 0, bad_cz = 0, bad_rtl = 0;
    long shown = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    chrI  = (F_chr)GetProcAddress(hs, "StrChrIW");
    rtlup = (WCHAR (NTAPI *)(WCHAR))GetProcAddress(hn, "RtlUpcaseUnicodeChar");
    if (!chrI || !rtlup) { printf("resolve failed\n"); return 1; }

    printf("== the known deviations, against every candidate ==\n");
    printf("   %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n",
           "code", "RTLup", "INVup", "LINGup", "USERup", "FOLDCZ", "matches'?'");
    {
        static const WCHAR DEV[] = { 0x01BB, 0x01BC, 0x01BD, 0x02B0, 0x02B2, 0x02B3,
                                     0x02E1, 0x02E2, 0x02E3, 0x1D2C, 0x1D2E, 0x1D43, 0x1D47 };
        unsigned k;
        for (k = 0; k < sizeof DEV / sizeof DEV[0]; ++k) {
            WCHAR ch = DEV[k];
            printf("   U+%04X   %04X     %04X     %04X     %04X     %04X\n",
                   ch, rtlup(ch),
                   lcmap(ch, LCMAP_UPPERCASE, LOCALE_INVARIANT),
                   lcmap(ch, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, LOCALE_INVARIANT),
                   lcmap(ch, LCMAP_UPPERCASE, LOCALE_USER_DEFAULT),
                   foldstr(ch, MAP_FOLDCZONE));
        }
    }

    printf("\n== now over ALL 65536 code units: which candidate's equality relation IS StrChrIW's? ==\n");
    printf("   for each c, compare 'c matches upcase-candidate(c)'s partner' the cheap way:\n");
    printf("   candidate(c) == candidate(d) must hold exactly when StrChrIW says c matches d,\n");
    printf("   tested against the 36 ASCII alphanumerics, which is where every known extra lands\n");
    {
        static const WCHAR AZ[] = {
            L'a',L'b',L'c',L'd',L'e',L'f',L'g',L'h',L'i',L'j',L'k',L'l',L'm',
            L'n',L'o',L'p',L'q',L'r',L's',L't',L'u',L'v',L'w',L'x',L'y',L'z',
            L'0',L'1',L'2',L'3',L'4',L'5',L'6',L'7',L'8',L'9' };
        unsigned k;
        for (c = 1; c <= 0xFFFF; ++c) {
            WCHAR fr = rtlup((WCHAR)c);
            WCHAR fi = lcmap((WCHAR)c, LCMAP_UPPERCASE, LOCALE_INVARIANT);
            WCHAR fl = lcmap((WCHAR)c, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, LOCALE_INVARIANT);
            WCHAR fu = lcmap((WCHAR)c, LCMAP_UPPERCASE, LOCALE_USER_DEFAULT);
            WCHAR fz = foldstr((WCHAR)c, MAP_FOLDCZONE);
            for (k = 0; k < sizeof AZ / sizeof AZ[0]; ++k) {
                int truth = same(AZ[k], (WCHAR)c);
                if ((rtlup(AZ[k]) == fr) != truth) ++bad_rtl;
                if ((lcmap(AZ[k], LCMAP_UPPERCASE, LOCALE_INVARIANT) == fi) != truth) ++bad_inv;
                if ((lcmap(AZ[k], LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, LOCALE_INVARIANT) == fl) != truth) ++bad_ling;
                if ((lcmap(AZ[k], LCMAP_UPPERCASE, LOCALE_USER_DEFAULT) == fu) != truth) ++bad_user;
                if ((foldstr(AZ[k], MAP_FOLDCZONE) == fz) != truth) {
                    ++bad_cz;
                    if (shown < 10) {
                        printf("   FOLDCZONE disagrees: U+%04X vs '%c' (StrChrIW %d)\n",
                               c, (char)AZ[k], truth);
                        ++shown;
                    }
                }
            }
        }
        printf("\n   disagreements with StrChrIW, over 65535 x 36 pairs:\n");
        printf("     RtlUpcaseUnicodeChar                  %ld\n", bad_rtl);
        printf("     LCMapStringW INVARIANT  UPPERCASE     %ld\n", bad_inv);
        printf("     LCMapStringW INVARIANT  +LINGUISTIC   %ld\n", bad_ling);
        printf("     LCMapStringW USERDEFAULT UPPERCASE    %ld\n", bad_user);
        printf("     FoldStringW  MAP_FOLDCZONE            %ld\n", bad_cz);
    }
    return 0;
}
