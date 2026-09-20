/* changes/263-rtlcompareunicodestrings/probes/fold.c
 *
 * The shape of the upcase table, because the implementation's speed depends on it being boring
 * where text usually lives.
 *
 * The plan for the case-insensitive path is the one change 236 arrived at: compare the RAW
 * characters first, because two strings that are equal are usually equal exactly, and fold only a
 * block that disagrees. That leaves one case where the fold is on the critical path for every
 * character, two strings that differ only in case, where every block disagrees raw and every
 * block has to be folded, and for that case a 65536-entry table lookup per character would be
 * slower than the shipped code.
 *
 * So the fold needs an in-vector form, and an in-vector form can only exist over a range where the
 * table is an ARITHMETIC RULE rather than a lookup. This measures exactly which ranges those are:
 *
 *   1. Is the ASCII quarter exactly "a-z becomes A-Z, everything else unchanged"? If it is, a block
 *      of characters all below 0x80 can be folded with two compares and a masked subtract, and real
 *      text takes that path.
 *   2. How many characters outside ASCII fold at all, and do the ones that fold share one offset?
 *      A second arithmetic range would be worth a second in-vector case; a scatter would not.
 *   3. Are there characters whose upcase is SMALLER than themselves, or that fold across a range
 *      boundary? Those are the ones that break a "subtract 32 if in range" rule quietly.
 *
 * Nothing here is assumed from change 165's finding that RtlUpperChar is plain ASCII for the 8-bit
 * case: that is a different function over a different domain, and this project has been caught four
 * times by a rule inherited from a sibling that looked like the same rule.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef WCHAR (NTAPI *F_Up)(WCHAR);

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Up up = (F_Up)GetProcAddress(h, "RtlUpcaseUnicodeChar");
    unsigned i;
    long ascii_bad = 0, folds = 0, folds_by_32 = 0, folds_up = 0, folds_down = 0;
    unsigned lo_fold = 0xFFFF, hi_fold = 0;
    long by_offset[8] = { 0 };
    static const int OFF[8] = { 32, 1, 8, 16, 26, 40, 48, 80 };

    if (!up) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== 1. THE ASCII QUARTER (0x0000..0x007F) ==\n");
    for (i = 0; i < 0x80; ++i) {
        WCHAR u = up((WCHAR)i);
        WCHAR want = (i >= 'a' && i <= 'z') ? (WCHAR)(i - 32) : (WCHAR)i;
        if (u != want) {
            if (++ascii_bad <= 10)
                printf("   U+%04X -> U+%04X, but \"a-z minus 32\" says U+%04X\n", i, u, want);
        }
    }
    printf("   %s (%ld disagreements)\n", ascii_bad ? "NOT the plain ASCII rule"
           : "EXACTLY a-z becomes A-Z and nothing else changes -- foldable in-vector", ascii_bad);

    printf("\n== 2. EVERYTHING ELSE (0x0080..0xFFFF) ==\n");
    for (i = 0x80; i < 0x10000; ++i) {
        WCHAR u = up((WCHAR)i);
        int d;
        if (u == (WCHAR)i) continue;
        ++folds;
        if (i < lo_fold) lo_fold = i;
        if (i > hi_fold) hi_fold = i;
        if ((int)u < (int)i) ++folds_down; else ++folds_up;
        if ((int)i - (int)u == 32) ++folds_by_32;
        for (d = 0; d < 8; ++d) if ((int)i - (int)u == OFF[d]) { ++by_offset[d]; break; }
    }
    printf("   characters that fold: %ld, from U+%04X to U+%04X\n", folds, lo_fold, hi_fold);
    printf("   folding DOWN (upcase is smaller): %ld;  folding UP: %ld\n", folds_down, folds_up);
    printf("   by offset:");
    for (i = 0; i < 8; ++i) printf("  -%d:%ld", OFF[i], by_offset[i]);
    printf("\n   of those, offset 32: %ld\n", folds_by_32);

    printf("\n== 3. THE LATIN-1 SUPPLEMENT (0x00A0..0x00FF), where a second range would be ==\n");
    {
        long l1 = 0, l1_by32 = 0;
        for (i = 0xA0; i < 0x100; ++i) {
            WCHAR u = up((WCHAR)i);
            if (u == (WCHAR)i) continue;
            ++l1;
            if ((int)i - (int)u == 32) ++l1_by32;
            if (l1 <= 8) printf("   U+%04X -> U+%04X (offset %d)\n", i, u, (int)i - (int)u);
        }
        printf("   %ld fold, %ld of them by 32\n", l1, l1_by32);
    }

    printf("\n== 4. WHAT A BLOCK-IS-ALL-ASCII TEST HAS TO PROTECT ==\n");
    printf("   a character at or above 0x80 can fold by an offset the ASCII rule does not use, so a\n");
    printf("   block containing one has to go through the table. The test is therefore \"every\n");
    printf("   character in both blocks is below 0x80\", which is one unsigned compare per block.\n");
    return 0;
}
