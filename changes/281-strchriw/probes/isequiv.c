/* changes/281-strchriw/probes/isequiv.c
 *
 * Is "StrChrIW matches" even an equivalence relation?
 *
 * Everything built so far assumes it is. probes/foldtable.c took a canonical representative per
 * code unit and called the result "classes"; tables.c groups members; impl.asm compares against the
 * members of the needle's class. All of that is meaningless unless the relation is SYMMETRIC and
 * TRANSITIVE, and nothing has checked that it is.
 *
 * The gate is now pointing at exactly that hole. It reported 66 mismatches in 206096 where OURS
 * Agreed with live (both NULL) and the model claimed a match -- meaning the table says two code
 * units share a class and the live export, asked directly, says they do not. probes/unfindable.c
 * ruled out the easy explanation: there are ZERO code units the export cannot find in a string of
 * themselves.
 *
 * So the remaining possibility is the one the design cannot survive: that `a finds b` is not the
 * same question as `b finds a`, or that a finds b and b finds c without a finding c. A relation
 * like that has no classes, and a class-based implementation cannot reproduce it.
 *
 * This asks, with no model in the way:
 *
 *   1. SYMMETRY, over a wide deterministic sample and over every code unit against a fixed alphabet.
 *   2. TRANSITIVITY, over the triples the representative table claims are classes.
 *   3. And specifically the U+D7Bx needles the gate flagged: what does the export say about them,
 *      pair by pair, next to what the table believes.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);
static F_chr chrI;

static int finds(WCHAR hay, WCHAR needle)
{
    wchar_t s[2]; s[0] = hay; s[1] = 0;
    return chrI(s, needle) != NULL;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    unsigned i, c;
    long asym = 0, shown = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    chrI = (F_chr)GetProcAddress(hs, "StrChrIW");
    if (!chrI) { printf("resolve failed\n"); return 1; }

    printf("== 1. SYMMETRY: is 'a finds b' the same as 'b finds a'? ==\n");
    for (i = 0; i < 400000; ++i) {
        unsigned a = (i * 15373u + 7u) & 0xFFFF;
        unsigned b = (i * 40503u + 13u) & 0xFFFF;
        if (!a || !b) continue;
        if (finds((WCHAR)a, (WCHAR)b) != finds((WCHAR)b, (WCHAR)a)) {
            ++asym;
            if (shown < 12) {
                printf("   ASYMMETRIC: U+%04X finds U+%04X = %d, but the other way = %d\n",
                       a, b, finds((WCHAR)a, (WCHAR)b), finds((WCHAR)b, (WCHAR)a));
                ++shown;
            }
        }
    }
    printf("   %ld asymmetric pairs in 400000 sampled\n", asym);

    printf("\n== 2. every code unit against a fixed alphabet, both directions ==\n");
    {
        static const WCHAR AZ[] = { L'a', L'e', L'k', L'2', L'5', 0x00AD, 0x1101, 0xD7B0 };
        long bad = 0;
        shown = 0;
        for (c = 1; c <= 0xFFFF; ++c) {
            unsigned k;
            for (k = 0; k < sizeof AZ / sizeof AZ[0]; ++k) {
                int f1 = finds(AZ[k], (WCHAR)c);
                int f2 = finds((WCHAR)c, AZ[k]);
                if (f1 != f2) {
                    ++bad;
                    if (shown < 12) {
                        printf("   ASYMMETRIC: U+%04X vs U+%04X : %d / %d\n", c, AZ[k], f1, f2);
                        ++shown;
                    }
                }
            }
        }
        printf("   %ld asymmetric (code unit, alphabet) pairs\n", bad);
    }

    printf("\n== 3. the needles the gate flagged, pair by pair ==\n");
    {
        static const WCHAR SUS[] = { 0xD7B0, 0xD7B1, 0xD7B2, 0xD7B4, 0xD7B5 };
        static const WCHAR PART[] = { 0x00AD, 0x1101, 0x1161, 0xD7B0, L'a' };
        unsigned k, m;
        for (k = 0; k < 5; ++k) {
            printf("   U+%04X finds:", SUS[k]);
            for (m = 0; m < 5; ++m)
                printf("  U+%04X=%d", PART[m], finds(PART[m], SUS[k]));
            printf("\n");
        }
        printf("\n   and what a HAYSTACK of all code units says (what gentable.c used):\n");
        {
            static wchar_t hay[65537];
            for (c = 1; c <= 0xFFFF; ++c) hay[c - 1] = (wchar_t)c;
            hay[0xFFFF] = 0;
            for (k = 0; k < 5; ++k) {
                PCWSTR p = chrI(hay, SUS[k]);
                printf("   StrChrIW(all-code-units, U+%04X) -> %s U+%04X\n", SUS[k],
                       p ? "offset of" : "NULL", p ? (unsigned)(p - hay + 1) : 0);
            }
            printf("\n   ^ if that representative is NOT found by the one-character test above,\n"
                   "     the relation depends on CONTEXT and has no classes at all.\n");
        }
    }

    printf("\n== 4. TRANSITIVITY over a known multi-member class ==\n");
    {
        static const WCHAR K[] = { L'K', L'k', 0x1D37, 0x1D4F, 0x212A };
        unsigned a2, b2;
        long bad = 0;
        for (a2 = 0; a2 < 5; ++a2)
            for (b2 = 0; b2 < 5; ++b2)
                if (!finds(K[a2], K[b2])) {
                    printf("   U+%04X does NOT find U+%04X\n", K[a2], K[b2]);
                    ++bad;
                }
        printf("   %ld failures inside the K class (K, k, superscripts, KELVIN SIGN)\n", bad);
    }
    return 0;
}
