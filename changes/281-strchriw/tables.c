/* changes/281-strchriw/tables.c
 *
 * THE TWO CASE TABLES, AND THE THREE FACTS THE VECTOR LOOP RESTS ON.
 *
 * probes/contract.c established that StrChrIW's notion of equality is EXACTLY the ordinal upcase
 * table -- `upcase(haystack) == upcase(needle)` -- and not a linguistic fold. 0 disagreements over
 * 3892 candidate pairs, and all 65535 code units matched themselves, their CharUpperW and their
 * CharLowerW. That is what makes this change writable at all: changes 274 and 276 parked because
 * the cost they measured was an OS call this project does not own, and a locale-dependent collation
 * would have been the same story.
 *
 * probes/classes.c then measured the fact the whole implementation is built on:
 *
 *     973 of 65536 code units change under upcase
 *     the LARGEST equivalence class is TWO -- there are ZERO classes with three members
 *     and the second member is ALWAYS downcase(upcase(c)), in all 973 cases
 *
 * So the search does not have to upcase the haystack at all. The needle is fixed for the whole
 * call, so the set of code units that can match it is just {U, downcase(U)} with U = upcase(needle)
 * -- TWO characters, found in two table lookups ONCE PER CALL. The inner loop is then two vector
 * compares per sixteen code units, with no table access whatsoever.
 *
 * THE TABLES ARE BUILT BY ASKING THE EXPORTS, not transcribed. That is change 277's decision, and
 * change 269's about the SDDL aliases, and change 210's about its upcase table: a transcribed table
 * is a constant nobody can check by reading it.
 *
 * AND THE THREE FACTS THE LOOP DEPENDS ON ARE CHECKED HERE RATHER THAN ASSUMED, because each is the
 * kind of thing that is true until it is not:
 *
 *   1. every class has at most two members -- otherwise two compares are not enough;
 *   2. the second member is downcase(upcase(c)) -- otherwise the partner lookup is wrong;
 *   3. downcase(U) is never 0 for U != 0 -- otherwise a match and the terminator could land on the
 *      same code unit and the loop could not tell "found it" from "end of string".
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned short wia_sci_up[65536];
unsigned short wia_sci_dn[65536];

typedef WCHAR (NTAPI *F_ch)(WCHAR);

/* Returns 0 on success, non-zero if the tables could not be built or do not satisfy the rules the
   vector path depends on. */
int wia_sci_init(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    F_ch up = (F_ch)GetProcAddress(hn, "RtlUpcaseUnicodeChar");
    F_ch dn = (F_ch)GetProcAddress(hn, "RtlDowncaseUnicodeChar");
    static unsigned short cnt[65536];
    int i;

    if (!up || !dn) return 1;
    for (i = 0; i < 65536; ++i) {
        wia_sci_up[i] = (unsigned short)up((WCHAR)i);
        wia_sci_dn[i] = (unsigned short)dn((WCHAR)i);
    }

    /* 1. no equivalence class may have more than two members */
    for (i = 0; i < 65536; ++i) ++cnt[wia_sci_up[i]];
    for (i = 0; i < 65536; ++i) if (cnt[i] > 2) return 2;

    /* 2. when a class has two members, the other one is downcase(upcase(c)) */
    for (i = 1; i < 65536; ++i) {
        unsigned u = wia_sci_up[i];
        if (cnt[u] < 2) continue;
        if ((unsigned)i != u && wia_sci_dn[u] != (unsigned short)i) return 3;
    }

    /* 3. downcase(U) is never the terminator for a non-terminator U -- if it were, a match and the
          end of the string could occupy the same code unit and the loop could not tell them apart */
    for (i = 1; i < 65536; ++i) if (wia_sci_dn[i] == 0) return 4;

    /* and tables that came back as the identity everywhere would be a wrong answer that looks like
       a right one -- 'a' has to move */
    if (wia_sci_up['a'] != 'A' || wia_sci_dn['A'] != 'a') return 5;
    if (wia_sci_up[0] != 0 || wia_sci_dn[0] != 0) return 6;
    return 0;
}
