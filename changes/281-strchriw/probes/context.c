/* changes/281-strchriw/probes/context.c
 *
 * The decisive experiment: Does a match depend on the surrounding characters?
 *
 * Everything this change is built on assumes StrChrIW asks a question about ONE haystack character
 * at a time: "is s[i] equal to the needle". If that is true, the relation is a 65536 x 65536 table,
 * probes/isequiv.c has already shown it symmetric and transitive on the cases it sampled, and a
 * class-based vector search reproduces it exactly.
 *
 * probes/isequiv.c turned up a contradiction that breaks the assumption open:
 *
 *     StrChrIW(all-code-units-in-order, U+D7B0)  ->  matches at the position of U+D7A2
 *     StrChrIW(L"힢", U+D7B0)                ->  (tested below)
 *
 * If the first matches and the second does not, the match at that position was not about U+D7A2 at
 * all; it was about U+D7A2 and what follows it. a collation-based search can do that: Hangul jamo
 * combine, and CompareStringW can consider a needle equal to a sequence.
 *
 * This is the question that decides whether change 281 can exist:
 *
 *   * if every match is decided by the single character at the match position, the relation is a
 *     table and this change proceeds;
 *   * if a match can depend on the characters AROUND it, then reproducing StrChrIW means
 *     reimplementing Windows collation, which is exactly the wall changes 274 and 276 hit, and
 *     change 281 parks with the reason written down.
 *
 * The test takes random strings, finds where the export matches, and asks whether the SAME needle
 * matches the SAME character standing alone. Any disagreement settles it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);
static F_chr chrI;

static int finds1(WCHAR hay, WCHAR needle)
{
    wchar_t s[2]; s[0] = hay; s[1] = 0;
    return chrI(s, needle) != NULL;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    unsigned long long rs = 0x243F6A8885A308D3ull;
    long ctx_dep = 0, shown = 0, tested = 0;
    long missed = 0;
    unsigned i;

    setvbuf(stdout, NULL, _IONBF, 0);
    chrI = (F_chr)GetProcAddress(hs, "StrChrIW");
    if (!chrI) { printf("resolve failed\n"); return 1; }

    printf("== the specific contradiction ==\n");
    printf("   U+D7B0 in a one-character string of U+D7A2 : %d\n", finds1(0xD7A2, 0xD7B0));
    printf("   U+D7B0 in a one-character string of U+D7B0 : %d\n", finds1(0xD7B0, 0xD7B0));
    {
        static wchar_t two[3];
        two[0] = 0xD7A2; two[1] = 0xD7A3; two[2] = 0;
        printf("   U+D7B0 in \"D7A2 D7A3\"                       : offset %d\n",
               chrI(two, 0xD7B0) ? (int)(chrI(two, 0xD7B0) - two) : -1);
        two[0] = 0xD7A2; two[1] = 0xD7B0; two[2] = 0;
        printf("   U+D7B0 in \"D7A2 D7B0\"                       : offset %d\n",
               chrI(two, 0xD7B0) ? (int)(chrI(two, 0xD7B0) - two) : -1);
    }

    printf("\n== the general test: 200000 random strings ==\n");
    printf("   for each match the export reports, does the needle match that character ALONE?\n");
    for (i = 0; i < 200000; ++i) {
        wchar_t s[17];
        WCHAR needle;
        PCWSTR p;
        int n, k;
        rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
        n = 1 + (int)(rs % 16);
        for (k = 0; k < n; ++k) {
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            s[k] = (wchar_t)(1 + (rs % 0xFFFF));
        }
        s[n] = 0;
        rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
        /* half the time use a needle that IS in the string, so matches are common */
        needle = (rs & 1) ? s[(rs >> 8) % n] : (wchar_t)(1 + ((rs >> 16) % 0xFFFF));

        p = chrI(s, needle);
        ++tested;
        if (p) {
            if (!finds1(*p, needle)) {
                ++ctx_dep;
                if (shown < 10) {
                    printf("   CONTEXT: needle U+%04X matched U+%04X at offset %d, but does NOT\n"
                           "            match U+%04X standing alone\n",
                           needle, (unsigned)*p, (int)(p - s), (unsigned)*p);
                    ++shown;
                }
            }
        } else {
            /* the export found nothing: no single character may match it either */
            for (k = 0; k < n; ++k)
                if (finds1(s[k], needle)) {
                    ++missed;
                    if (shown < 10) {
                        printf("   CONTEXT: needle U+%04X matches U+%04X alone, but the export\n"
                               "            found NOTHING in the %d-character string holding it\n",
                               needle, (unsigned)s[k], n);
                        ++shown;
                    }
                    break;
                }
        }
    }
    printf("\n   %ld strings tested\n", tested);
    printf("   matches whose character does NOT match alone : %ld\n", ctx_dep);
    printf("   misses that a single character WOULD match   : %ld\n", missed);
    if (!ctx_dep && !missed)
        printf("\n   -> every match is decided by ONE character. The relation is a table and\n"
               "      change 281 proceeds.\n");
    else
        printf("\n   -> A MATCH DEPENDS ON CONTEXT. StrChrIW is a collation search, not a\n"
               "      character test, and no per-character table can reproduce it.\n");
    return 0;
}
