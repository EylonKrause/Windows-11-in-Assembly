/* changes/281-strchriw/probes/foldtable.c
 *
 * The fold relation, taken from the function itself rather than guessed at.
 *
 * Three probes have now narrowed this and two of them were wrong:
 *
 *   contract.c   "the rule IS the ordinal upcase table, exactly" -- WRONG. It built its candidate
 *                pairs out of CharUpperW/CharLowerW/RtlUpcase/RtlDowncase, so a pair that none of
 *                those four relates -- (U+1D2C, 'a') -- could never be asked about. Zero
 *                disagreements over the pairs it could generate, and it generated the wrong pairs.
 *   widerfold.c  settled what it is NOT: not linguistic. e-acute does not find 'e', n-tilde does
 *                not find 'n', fullwidth 'a' does not find 'a', sharp s does not expand.
 *   whichfold.c  found FoldStringW(MAP_FOLDCZONE) reproduces the compatibility half exactly
 *                (U+1D2C -> 'A', U+02B0 -> 'h') but does no case folding, so the rule is a
 *                COMPOSITION -- and the composition still fails on U+01BB..U+01BD, which fold onto
 *                digits.
 *
 * Guessing the next formula would be a fourth hypothesis. Instead this takes the relation directly.
 *
 * THE TRICK: a haystack containing every code unit 1..65535 in ascending order. StrChrIW returns
 * the FIRST match, so one call per needle yields the SMALLEST code unit that the export considers
 * equal to it -- a canonical representative, straight from the function, with no hypothesis at all.
 * 65535 calls, each scanning until it hits, is a couple of minutes; the whole relation falls out.
 *
 * With the ground truth in hand, the candidate formulas are then scored against it, so that
 * tables.c can use a CHEAP one that is known to be exact rather than a slow one that is merely
 * believed to be.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);
static F_chr chrI;
static WCHAR (NTAPI *rtlup)(WCHAR);

static wchar_t hay[65537];
static unsigned short rep[65536];

static WCHAR lcmap(WCHAR c, DWORD flags, LCID loc)
{
    wchar_t in[2], out[8]; int n;
    in[0] = c; in[1] = 0;
    n = LCMapStringW(loc, flags, in, 1, out, 8);
    return (n == 1) ? out[0] : c;
}
static WCHAR fold(WCHAR c, DWORD flags)
{
    wchar_t in[2], out[8]; int n;
    in[0] = c; in[1] = 0;
    n = FoldStringW(flags, in, 1, out, 8);
    return (n == 1) ? out[0] : c;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    unsigned c;
    LARGE_INTEGER t0, t1, fq;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&fq);
    chrI  = (F_chr)GetProcAddress(hs, "StrChrIW");
    rtlup = (WCHAR (NTAPI *)(WCHAR))GetProcAddress(hn, "RtlUpcaseUnicodeChar");
    if (!chrI || !rtlup) { printf("resolve failed\n"); return 1; }

    for (c = 1; c <= 0xFFFF; ++c) hay[c - 1] = (wchar_t)c;
    hay[0xFFFF] = 0;

    printf("== taking the fold relation straight from StrChrIW (65535 calls) ==\n");
    QueryPerformanceCounter(&t0);
    for (c = 1; c <= 0xFFFF; ++c) {
        PCWSTR p = chrI(hay, (WCHAR)c);
        rep[c] = p ? (unsigned short)(p - hay + 1) : (unsigned short)c;
        if ((c & 0x1FFF) == 0) printf("   ... %u\n", c);
    }
    QueryPerformanceCounter(&t1);
    printf("   done in %.1f s\n", (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart);

    {
        long classes = 0, maxsize = 0;
        static unsigned short cnt[65536];
        for (c = 1; c <= 0xFFFF; ++c) ++cnt[rep[c]];
        for (c = 0; c <= 0xFFFF; ++c) {
            if (cnt[c]) ++classes;
            if (cnt[c] > maxsize) maxsize = cnt[c];
        }
        printf("\n   distinct classes: %ld    LARGEST CLASS: %ld\n", classes, maxsize);
        printf("   (contract.c believed the largest class was 2; that was the wrong table)\n");
        printf("\n   every class with three or more members:\n");
        {
            long shown = 0;
            unsigned r;
            for (r = 1; r <= 0xFFFF; ++r) {
                if (cnt[r] < 3) continue;
                printf("     rep %04X <- ", r);
                for (c = 1; c <= 0xFFFF; ++c) if (rep[c] == r) printf("%04X ", c);
                printf("\n");
                if (++shown >= 30) { printf("     ... (stopping at 30)\n"); break; }
            }
            if (!shown) printf("     none\n");
        }
    }

    printf("\n== scoring the candidate formulas against the ground truth ==\n");
    printf("   a formula F is correct iff F(a)==F(b) exactly when rep[a]==rep[b]\n");
    {
        unsigned a;
        long bad_rtl = 0, bad_cz = 0, bad_czd = 0, bad_inv = 0;
        static unsigned short f_rtl[65536], f_cz[65536], f_czd[65536], f_inv[65536];
        for (a = 1; a <= 0xFFFF; ++a) {
            f_rtl[a] = rtlup((WCHAR)a);
            f_cz[a]  = rtlup(fold((WCHAR)a, MAP_FOLDCZONE));
            f_czd[a] = rtlup(fold((WCHAR)a, MAP_FOLDCZONE | MAP_FOLDDIGITS));
            f_inv[a] = lcmap(fold((WCHAR)a, MAP_FOLDCZONE | MAP_FOLDDIGITS),
                             LCMAP_UPPERCASE, LOCALE_INVARIANT);
        }
        /* two characters are in the same class iff they share a rep; a formula must induce the
           SAME partition. Compare by canonicalising: for each class, every member must share one
           formula value, and no two classes may share one. */
        {
            static unsigned short seen_rtl[65536], seen_cz[65536], seen_czd[65536], seen_inv[65536];
            for (a = 1; a <= 0xFFFF; ++a) {
                unsigned r = rep[a];
                if (!seen_rtl[r]) { seen_rtl[r] = f_rtl[a]; seen_cz[r] = f_cz[a];
                                    seen_czd[r] = f_czd[a]; seen_inv[r] = f_inv[a]; }
                else {
                    if (seen_rtl[r] != f_rtl[a]) ++bad_rtl;
                    if (seen_cz[r]  != f_cz[a])  ++bad_cz;
                    if (seen_czd[r] != f_czd[a]) ++bad_czd;
                    if (seen_inv[r] != f_inv[a]) ++bad_inv;
                }
            }
        }
        printf("     RtlUpcase                                    splits %ld class(es)\n", bad_rtl);
        printf("     RtlUpcase o FOLDCZONE                        splits %ld\n", bad_cz);
        printf("     RtlUpcase o (FOLDCZONE|FOLDDIGITS)           splits %ld\n", bad_czd);
        printf("     LCMap(INVARIANT,UPPER) o (FOLDCZONE|DIGITS)  splits %ld\n", bad_inv);
        printf("   (0 splits means the formula never separates two characters the export unites;\n"
               "    it must ALSO not unite two the export separates -- checked next)\n");
        {
            long merge_czd = 0, merge_cz = 0;
            static unsigned short owner_czd[65536], owner_cz[65536];
            for (a = 1; a <= 0xFFFF; ++a) {
                if (!owner_czd[f_czd[a]]) owner_czd[f_czd[a]] = (unsigned short)rep[a];
                else if (owner_czd[f_czd[a]] != rep[a]) ++merge_czd;
                if (!owner_cz[f_cz[a]]) owner_cz[f_cz[a]] = (unsigned short)rep[a];
                else if (owner_cz[f_cz[a]] != rep[a]) ++merge_cz;
            }
            printf("     RtlUpcase o FOLDCZONE                        merges %ld\n", merge_cz);
            printf("     RtlUpcase o (FOLDCZONE|FOLDDIGITS)           merges %ld\n", merge_czd);
        }
    }
    return 0;
}
