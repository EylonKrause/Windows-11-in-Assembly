/* changes/285-strcspniw/probes/relation.c
 *
 * Is StrCSpnIW's relation change 281's relation?  Extract it and diff it.
 *
 * probes/contract.c turned up a contradiction that cannot be left as an impression:
 *
 *     str {a,b,ZERO width SPACE,c},  set {soft hyphen}   ->  4   (no match at all)
 *
 * Change 281 measured the soft hyphen and the zero width space as matching each other -- they are two
 * of the 3237 ignorables, which form the largest set in that relation, and change 283's corpus 4
 * asserts exactly that pair and passes against the live StrRStrIW. So either StrCSpnIW uses a
 * DIFFERENT relation, or the ignorables behave differently on the set side.
 *
 * The same probe also showed the intransitive triple working precisely as change 281 has it: a set of
 * {D7A2} accepts both D7B0 and D7B1, while a set of {D7B0} rejects D7B1. So this is not simply an
 * ordinal comparison either -- change 281's first contract probe concluded "ordinal upcase table,
 * exactly" and was wrong, caught by 8 mismatches in 140561 cases, so that guess gets no credit here.
 *
 * So the relation is EXTRACTED rather than guessed. StrCSpnIW over a ONE-CHARACTER string is a direct
 * membership oracle:
 *
 *     StrCSpnIW({c, 0}, {m, 0}) == 0   <=>   m accepts c
 *
 * which gives a full row of the relation for any member m in 65535 calls. Each row is compared
 * against change 281's tables, and every disagreement is printed with its code units so the shape of
 * the difference is visible rather than summarised.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FSPN)(PCWSTR, PCWSTR);
static FSPN cspn;

int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern const unsigned char wia_sci_n[];

static const unsigned short MEMBERS[] = {
    0x0041,     /* 'A' -- plain ASCII */
    0x004B,     /* 'K' -- five partners, including the KELVIN SIGN */
    0x0020,     /* space -- two partners */
    0x00AD,     /* SOFT HYPHEN -- ignorable, matches a NUL */
    0x200B,     /* ZERO WIDTH SPACE -- ignorable, does NOT match a NUL */
    0x034F,     /* COMBINING GRAPHEME JOINER -- ignorable */
    0xD7A2,     /* the centre of the intransitive triple */
    0xD7B0,
    0x0130,     /* LATIN CAPITAL LETTER I WITH DOT ABOVE */
    0x212A,     /* KELVIN SIGN */
    0x00DF,     /* sharp s */
    0x1E9E,     /* capital sharp s */
};

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    static wchar_t str[4], set[4];
    unsigned mi, c;
    long total = 0, diff = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    cspn = (FSPN)GetProcAddress(hs, "StrCSpnIW");
    if (!cspn) { printf("no StrCSpnIW\n"); return 2; }
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }

    printf("== StrCSpnIW's relation, extracted and diffed against change 281's ==\n");
    printf("   oracle: StrCSpnIW({c},{m}) == 0  <=>  the set member m accepts the string unit c\n\n");

    for (mi = 0; mi < sizeof(MEMBERS) / sizeof(MEMBERS[0]); ++mi) {
        unsigned m = MEMBERS[mi];
        long rowdiff = 0, rowlive = 0, row281 = 0, shown = 0;
        set[0] = (wchar_t)m; set[1] = 0;
        for (c = 1; c < 65536; ++c) {
            int live, ours;
            str[0] = (wchar_t)c; str[1] = 0;
            live = (cspn(str, set) == 0);
            ours = wia_sci_match(m, c) ? 1 : 0;
            if (live) ++rowlive;
            if (ours) ++row281;
            ++total;
            if (live != ours) {
                ++diff; ++rowdiff;
                if (shown < 6) {
                    printf("     U+%04X accepts U+%04X:  live %s,  change 281 %s\n",
                           m, c, live ? "YES" : "no ", ours ? "YES" : "no ");
                    ++shown;
                }
            }
        }
        printf("   member U+%04X (n=%3d):  live accepts %5ld,  281 accepts %5ld,  DISAGREE %5ld%s\n",
               m, (int)wia_sci_n[m], rowlive, row281, rowdiff, rowdiff ? "  <--" : "");
    }

    printf("\n   %ld pairs tested, %ld disagreements\n", total, diff);
    if (!diff)
        printf("   THE RELATIONS AGREE on every member tested -- change 281's tables apply unchanged\n");
    else
        printf("   THE RELATIONS DIFFER -- change 281's tables do NOT apply as they stand\n");

    printf("\n-- and the reverse direction, to check symmetry of the export itself\n");
    {
        unsigned a, b;
        long asym = 0, n = 0;
        static const unsigned short P[] = { 0x0041, 0x0061, 0x004B, 0x212A, 0x00AD, 0x200B,
                                            0xD7A2, 0xD7B0, 0xD7B1, 0x0130, 0x0069, 0x0049 };
        for (a = 0; a < sizeof(P) / sizeof(P[0]); ++a) {
            for (b = 0; b < sizeof(P) / sizeof(P[0]); ++b) {
                int ab, ba;
                str[0] = (wchar_t)P[b]; str[1] = 0; set[0] = (wchar_t)P[a]; set[1] = 0;
                ab = (cspn(str, set) == 0);
                str[0] = (wchar_t)P[a]; str[1] = 0; set[0] = (wchar_t)P[b]; set[1] = 0;
                ba = (cspn(str, set) == 0);
                ++n;
                if (ab != ba) {
                    ++asym;
                    printf("     ASYMMETRIC: set U+%04X accepts U+%04X = %d, but set U+%04X "
                           "accepts U+%04X = %d\n", P[a], P[b], ab, P[b], P[a], ba);
                }
            }
        }
        printf("   %ld ordered pairs, %ld asymmetric\n", n, asym);
    }

    printf("\n-- does a MULTI-member set behave as the union of its members' rows?\n");
    {
        static const unsigned short S2[] = { 0x0041, 0x004B, 0x00AD, 0xD7A2 };
        unsigned i;
        long bad = 0;
        static wchar_t bigset[8];
        for (i = 0; i < 4; ++i) bigset[i] = (wchar_t)S2[i];
        bigset[4] = 0;
        for (c = 1; c < 65536; ++c) {
            int live, uni = 0;
            str[0] = (wchar_t)c; str[1] = 0;
            live = (cspn(str, bigset) == 0);
            for (i = 0; i < 4; ++i) {
                set[0] = (wchar_t)S2[i]; set[1] = 0;
                if (cspn(str, set) == 0) { uni = 1; break; }
            }
            if (live != uni) {
                if (bad < 6)
                    printf("     U+%04X: the four-member set says %d, the union of the rows says %d\n",
                           c, live, uni);
                ++bad;
            }
        }
        printf("   a set of {A,K,SHY,D7A2}: %ld code units where the set differs from the union\n",
               bad);
    }

    return 0;
}
