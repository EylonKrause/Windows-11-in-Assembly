/* changes/281-strchriw/probes/classes.c
 *
 * HOW MANY CODE UNITS CAN SHARE ONE UPCASE? THE WHOLE IMPLEMENTATION HANGS ON THE ANSWER.
 *
 * probes/contract.c settled that StrChrIW's notion of equality is EXACTLY the ordinal upcase table:
 * `upcase(haystack_char) == upcase(needle)`. The obvious way to vectorise that is to upcase every
 * character of the haystack and compare -- which means a table lookup per character, or an AVX2
 * gather per eight, and gathers are slow enough to throw the win away.
 *
 * There is a much better way, and it depends on one measurement. The needle is FIXED for the whole
 * call, so `upcase(w) == upcase(needle)` is really "is w a member of the set of code units that
 * upcase to U". If that set is SMALL and can be found in one lookup at call time, the inner loop
 * stops being a table lookup per character and becomes k vector compares per sixteen characters,
 * with k tiny.
 *
 * So: build the full 65536-entry upcase map from the live ntdll export, INVERT it, and report
 *
 *   - the largest class,
 *   - how many classes have more than one member (that is the size of the table to carry),
 *   - and every class with three or more members, listed, because those are the ones a
 *     "just check c and c^0x20" implementation would silently get wrong.
 *
 * It also checks the shortcut that would make the table unnecessary -- whether the class is always
 * {U, downcase(U)} -- because if that held, no table would be needed at all.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static WCHAR (NTAPI *rtlup)(WCHAR);
static WCHAR (NTAPI *rtldn)(WCHAR);

static WCHAR upmap[0x10000];
static unsigned short cnt[0x10000];

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    unsigned c, u;
    long classes_gt1 = 0, classes_gt2 = 0, maxsize = 0, changed = 0;
    long dn_ok = 0, dn_bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    rtlup = (WCHAR (NTAPI *)(WCHAR))GetProcAddress(hn, "RtlUpcaseUnicodeChar");
    rtldn = (WCHAR (NTAPI *)(WCHAR))GetProcAddress(hn, "RtlDowncaseUnicodeChar");
    if (!rtlup) { printf("resolve failed\n"); return 1; }

    for (c = 0; c < 0x10000; ++c) {
        upmap[c] = rtlup((WCHAR)c);
        if (upmap[c] != (WCHAR)c) ++changed;
        ++cnt[upmap[c]];
    }
    printf("== the ordinal upcase map, from live ntdll!RtlUpcaseUnicodeChar ==\n");
    printf("   code units whose upcase differs from themselves: %ld of 65536\n", changed);

    for (u = 0; u < 0x10000; ++u) {
        if (cnt[u] > maxsize) maxsize = cnt[u];
        if (cnt[u] > 1) ++classes_gt1;
        if (cnt[u] > 2) ++classes_gt2;
    }
    printf("   distinct upcase values with MORE THAN ONE member: %ld\n", classes_gt1);
    printf("   ...with three or more:                            %ld\n", classes_gt2);
    printf("   LARGEST CLASS:                                    %ld\n", maxsize);

    printf("\n== every class with three or more members ==\n");
    printf("   (a 'check c and c^0x20' implementation gets exactly these wrong)\n");
    {
        long shown = 0;
        for (u = 0; u < 0x10000; ++u) {
            if (cnt[u] < 3) continue;
            printf("   upcase %04X <-  ", u);
            for (c = 0; c < 0x10000; ++c)
                if (upmap[c] == (WCHAR)u) printf("%04X ", c);
            printf("\n");
            if (++shown >= 40) { printf("   ... (stopping at 40)\n"); break; }
        }
        if (!shown) printf("   none\n");
    }

    printf("\n== is the class always {U, downcase(U)}? ==\n");
    for (u = 1; u < 0x10000; ++u) {
        WCHAR d;
        if (cnt[u] != 2) continue;
        d = rtldn ? rtldn((WCHAR)u) : 0;
        /* the two members are u itself (when upmap[u]==u) and one other; is the other downcase(u)? */
        {
            unsigned other = 0xFFFFFFFFu;
            for (c = 0; c < 0x10000; ++c)
                if (upmap[c] == (WCHAR)u && c != u) { other = c; break; }
            if (other == d) ++dn_ok;
            else {
                if (dn_bad < 12)
                    printf("   upcase %04X: members {%04X, %04X} but downcase(%04X) = %04X\n",
                           u, u, other, u, d);
                ++dn_bad;
            }
        }
    }
    printf("   two-member classes where the partner IS downcase(U): %ld\n", dn_ok);
    printf("   two-member classes where it is NOT:                  %ld\n", dn_bad);
    if (dn_bad)
        printf("   -> a {U, downcase(U)} shortcut is WRONG; the class table is required\n");
    else
        printf("   -> {U, downcase(U)} covers every two-member class\n");

    printf("\n== how big is the table the implementation has to carry? ==\n");
    printf("   %ld classes with >1 member, largest %ld -> a sorted (upcase, members) table of\n",
           classes_gt1, maxsize);
    printf("   %ld entries, looked up ONCE PER CALL rather than once per character.\n", classes_gt1);
    return 0;
}
