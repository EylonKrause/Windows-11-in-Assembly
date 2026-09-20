/* changes/252-rtlfindunicodesubstring/probes/classsize.c
 *
 * The benchmark exposed a weak row and this asks whether it can be removed outright.
 *
 * The first implementation filters the insensitive search with an ASCII fold plus the clause "a
 * non-ASCII haystack unit is always a candidate". That clause is what makes the filter a superset
 * rather than an approximation, and it is unavoidable only because an ASCII fold cannot bring
 * U+00E0 and U+00C0 together. Its cost is that on text that is entirely non-ASCII, Cyrillic,
 * Greek, Hebrew, CJK, which is not an adversarial input but simply most of the world's text, the
 * filter admits every position and the scalar verifier runs at every one of them. Measured: 2.33x
 * over the shipped code, against 16x on ASCII and 35x case-sensitive.
 *
 * The alternative is to stop folding the haystack and start enumerating the needle. The anchor test
 * "upcase(hay) == upcase(needle[0])" is equivalent to "hay is a member of the case-equivalence
 * class of needle[0]", and that class can be enumerated ONCE per call, for the two anchor
 * characters only, and then tested with plain VPCMPEQW against each member. No fold on the
 * haystack at all, and EXACT rather than a superset: candidate density drops to the true match
 * density even on non-ASCII text.
 *
 * That is only worth building if the classes are small, because each member costs one compare and
 * one OR per anchor per block, and a class of twenty would be worse than the fold it replaces.
 * This measures the class-size distribution of the ordinal upcase table, and prints the largest
 * classes in full so the answer can be checked by eye rather than trusted.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef WCHAR (NTAPI *FUP)(WCHAR);

static unsigned short up[65536];
static unsigned char  count[65536];      /* class size, indexed by the class representative */

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    FUP u = (FUP)GetProcAddress(h, "RtlUpcaseUnicodeChar");
    unsigned i;
    unsigned hist[16];
    unsigned maxsz = 0, nclass = 0;

    if (!u) { printf("RtlUpcaseUnicodeChar not found\n"); return 1; }
    for (i = 0; i < 65536; ++i) up[i] = (unsigned short)u((WCHAR)i);
    for (i = 0; i < 65536; ++i) if (count[up[i]] < 255) ++count[up[i]];

    for (i = 0; i < 16; ++i) hist[i] = 0;
    for (i = 0; i < 65536; ++i) {
        if (!count[i]) continue;
        ++nclass;
        if (count[i] > maxsz) maxsz = count[i];
        hist[count[i] < 15 ? count[i] : 15]++;
    }

    printf("CASE-EQUIVALENCE CLASSES of the ordinal upcase table\n\n");
    printf("  distinct classes: %u   largest class: %u member(s)\n\n", nclass, maxsz);
    printf("  size  how many classes\n");
    for (i = 1; i < 16; ++i)
        if (hist[i]) printf("  %4s  %u\n", i == 15 ? ">=15" : (i == 1 ? "1" : (i == 2 ? "2" :
                            (i == 3 ? "3" : (i == 4 ? "4" : (i == 5 ? "5" : (i == 6 ? "6" :
                            (i == 7 ? "7" : "8+")))))))  , hist[i]);

    printf("\n  EVERY class with three or more members, in full:\n");
    for (i = 0; i < 65536; ++i) {
        if (count[i] < 3) continue;
        printf("    upcase U+%04X <-", i);
        {
            unsigned j;
            for (j = 0; j < 65536; ++j) if (up[j] == i) printf(" U+%04X", j);
        }
        printf("\n");
    }

    printf("\n  VERDICT: a filter that compares against every member of the anchor's class needs\n");
    printf("           %u compare(s) per anchor in the worst case.\n", maxsz);
    return 0;
}
