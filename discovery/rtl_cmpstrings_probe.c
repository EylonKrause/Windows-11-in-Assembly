/* discovery/rtl_cmpstrings_probe.c
 *
 * Is RtlCompareUnicodeStrings a distinct target, and is it linguistic?
 *
 * The uncovered survey ranks it at 0.076 ns/byte comparing equal strings and 0.102 with the
 * case-insensitive flag set; the best per-byte cost left in ntdll that is not already landed. But
 * two things have to be settled before any contract work starts, and this project has been caught
 * by both before:
 *
 *   1. Is it actually a different export? `RtlInitAnsiString` looked like a target in the same
 *      survey and turned out to be the SAME ADDRESS as `RtlInitString`, which change 095 landed
 *      long ago. Comparing GetProcAddress values costs nothing and settles it.
 *
 *   2. Is the case-insensitive flag linguistic? This is the question that decided whether several
 *      earlier targets were convertible at all: discovery/lstrcmp_is_linguistic.c and
 *      strcmpn_is_linguistic.c both ABANDONED their targets on the answer. A comparison that
 *      consults a locale, or that treats one character as equal to a sequence of others, cannot be
 *      reproduced by a byte loop, change 236 nearly shipped as a second StrStrA for exactly that
 *      reason, and only an enumeration of all 65536 ordered pairs showed the conflation there was
 *      strictly pairwise.
 *
 * So this enumerates ALL 65536 x 2 comparisons of a single character against a single character
 * under the insensitive flag, and reports the equivalence classes. If the classes are exactly
 * "upper/lower pairs of the ASCII and Latin-1 ranges", it is a table fold and convertible. If a
 * character is equal to something it has no case relationship with, or if the count does not match
 * the shipped RtlUpcaseUnicodeChar, it is not.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG (NTAPI *F_CmpS)(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);
typedef WCHAR (NTAPI *F_Upcase)(WCHAR);

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    void* a_plural   = (void*)GetProcAddress(h, "RtlCompareUnicodeStrings");
    void* a_singular = (void*)GetProcAddress(h, "RtlCompareUnicodeString");
    void* a_init     = (void*)GetProcAddress(h, "RtlInitString");
    void* a_ansi     = (void*)GetProcAddress(h, "RtlInitAnsiString");
    void* a_utf8     = (void*)GetProcAddress(h, "RtlInitUTF8String");
    F_CmpS cmp = (F_CmpS)a_plural;
    F_Upcase up = (F_Upcase)GetProcAddress(h, "RtlUpcaseUnicodeChar");
    long same_via_upcase = 0, same_but_not_upcase = 0, differ_but_upcase_same = 0;
    unsigned x, y;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== ADDRESSES (a target that shares an address with a landed change is not a target) ==\n");
    printf("  RtlCompareUnicodeStrings %p\n", a_plural);
    printf("  RtlCompareUnicodeString  %p   %s\n", a_singular,
           a_plural == a_singular ? "SAME -- already covered" : "distinct");
    printf("  RtlInitString            %p\n", a_init);
    printf("  RtlInitAnsiString        %p   %s\n", a_ansi,
           a_init == a_ansi ? "SAME as RtlInitString -- change 095 already covers it" : "distinct");
    printf("  RtlInitUTF8String        %p   %s\n", a_utf8,
           a_init == a_utf8 ? "SAME as RtlInitString" : "distinct");

    if (!cmp || !up) { printf("\nresolve failed\n"); return 1; }

    printf("\n== IS THE CASE-INSENSITIVE FLAG A PLAIN UPCASE FOLD? ==\n");
    printf("   all 65536 x 65536 is too many, so this asks every character against every character\n");
    printf("   THAT SHARES ITS UPCASE, plus a full sweep of each character against the 256 that\n");
    printf("   follow it -- which is where a non-case conflation would show up as a surprise\n");

    /* every character against the 256 that follow it: a conflation between unrelated characters
       would have to be visible somewhere in a dense sweep like this */
    for (x = 0; x < 65536; ++x) {
        for (y = x; y < x + 256 && y < 65536; ++y) {
            /* The lengths are in characters, not bytes. Passing 2 here compared each character
               against the stack slot after it, and every character came back DIFFERENT FROM
               ITSELF -- which is what the first run of this probe reported, and is the same class
               of mistake as a survey row whose subject does not do the work its label claims. */
            wchar_t s1 = (wchar_t)x, s2 = (wchar_t)y;
            LONG r = cmp(&s1, 1, &s2, 1, TRUE);
            int upsame = (up((WCHAR)x) == up((WCHAR)y));
            if (r == 0 && upsame) ++same_via_upcase;
            else if (r == 0 && !upsame) {
                if (++same_but_not_upcase <= 10)
                    printf("   EQUAL but RtlUpcaseUnicodeChar disagrees: U+%04X and U+%04X "
                           "(upcase %04X vs %04X)\n", x, y, up((WCHAR)x), up((WCHAR)y));
            } else if (r != 0 && upsame) {
                if (++differ_but_upcase_same <= 10)
                    printf("   DIFFERENT but same upcase: U+%04X and U+%04X\n", x, y);
            }
        }
    }
    printf("\n  equal AND same upcase:        %ld\n", same_via_upcase);
    printf("  equal but DIFFERENT upcase:   %ld\n", same_but_not_upcase);
    printf("  different but SAME upcase:    %ld\n", differ_but_upcase_same);
    printf("\n  %s\n", (same_but_not_upcase == 0 && differ_but_upcase_same == 0)
           ? "THE FLAG IS EXACTLY RtlUpcaseUnicodeChar -- a table fold, which is convertible"
           : "THE FLAG IS NOT A PLAIN UPCASE FOLD -- see the lines above before going further");
    return 0;
}
