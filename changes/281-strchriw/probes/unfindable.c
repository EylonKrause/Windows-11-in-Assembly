/* changes/281-strchriw/probes/unfindable.c
 *
 * Are there code units StrChrIW cannot find even when they are present?
 *
 * The rebuilt gate reported 66 mismatches in 206096, and in every one of them ours agreed with live
 * -- both returned NULL -- and the scalar MODEL was the odd one out, claiming a match. The needles
 * were U+D7B0..U+D7BE, Hangul jamo extended.
 *
 * The model says a match exists because the table says the needle and the haystack character share
 * a class. They do share a class: they are the same character. So the only way live can return NULL
 * is if StrChrIW cannot find these code units at all, even when searching a string that is nothing
 * but that code unit.
 *
 * That would be a contract fact nothing so far has captured, and it is the kind of fact that has to
 * be MEASURED rather than reasoned about: probes/contract.c reasoned, and was wrong.
 *
 * So: ask the export, for every one of the 65535 code units, whether it can find that code unit in
 * a one-character string containing exactly it. Anything that answers NULL is a needle that never
 * matches anything, and the implementation has to reproduce that.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    F_chr chrI = hs ? (F_chr)GetProcAddress(hs, "StrChrIW") : 0;
    unsigned c;
    long unfind = 0, shown = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!chrI) { printf("resolve failed\n"); return 1; }

    printf("== code units StrChrIW cannot find in a string made of that code unit ==\n");
    for (c = 1; c <= 0xFFFF; ++c) {
        wchar_t s[2];
        s[0] = (wchar_t)c; s[1] = 0;
        if (chrI(s, (WCHAR)c) == 0) {
            ++unfind;
            if (shown < 64) { printf(" %04X", c); if ((++shown % 16) == 0) printf("\n"); }
        }
    }
    printf("\n\n   %ld of 65535 code units are UNFINDABLE -- the export returns NULL even though\n"
           "   the character is right there\n", unfind);

    printf("\n== are they unfindable as a HAYSTACK character too, or only as a NEEDLE? ==\n");
    for (c = 1; c <= 0xFFFF; ++c) {
        wchar_t s[4];
        s[0] = (wchar_t)c; s[1] = 0;
        if (chrI(s, (WCHAR)c) != 0) continue;
        /* it is unfindable as a needle. Can a normal needle find it? Put it next to an 'a'. */
        s[0] = (wchar_t)c; s[1] = L'a'; s[2] = 0;
        printf("   U+%04X: searching \"<it>a\" for 'a' gives offset %d;", c,
               chrI(s, L'a') ? (int)(chrI(s, L'a') - s) : -1);
        s[0] = L'a'; s[1] = (wchar_t)c; s[2] = 0;
        printf("  \"a<it>\" for 'a' gives %d\n", chrI(s, L'a') ? (int)(chrI(s, L'a') - s) : -1);
        if (--shown < -8) break;
    }

    printf("\n== and what does a plain ordinal search say about the same characters? ==\n");
    {
        F_chr chrW = (F_chr)GetProcAddress(hs, "StrChrW");
        if (chrW) {
            long diff = 0;
            for (c = 1; c <= 0xFFFF; ++c) {
                wchar_t s[2];
                s[0] = (wchar_t)c; s[1] = 0;
                if ((chrI(s, (WCHAR)c) == 0) != (chrW(s, (WCHAR)c) == 0)) ++diff;
            }
            printf("   StrChrW (case-SENSITIVE) disagrees with StrChrIW on findability for %ld\n"
                   "   code units -- so this is a property of the case-insensitive path only\n", diff);
        }
    }
    return 0;
}
