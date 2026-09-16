/* changes/252-rtlfindunicodesubstring/casemate.c
 *
 * THE CASE-PARTNER TABLE: wia_casemate[c] is the OTHER member of c's case-equivalence class under
 * the ordinal upcase, or c itself when the class is a singleton.
 *
 * WHY THIS EXISTS RATHER THAN A FOLD. The test a case-insensitive search needs is
 *
 *      upcase(hay) == upcase(needle)
 *
 * which is the same as "hay is a member of the class of needle". probes/classsize.c measured that
 * distribution over the whole ordinal table: 64563 distinct classes, 63590 of them singletons, 973
 * of size two, AND NOTHING LARGER. So the membership test is exactly two comparisons -- which a
 * vector unit can do, and a fold cannot, because there is no arithmetic that brings U+00E0 and
 * U+00C0 together without also bringing unrelated units together. See impl.asm's header for what
 * that replaced and what it measured.
 *
 * IT IS DERIVED FROM CHANGE 210's TABLE, not built from a second pass over the OS. That table is
 * already this project's single point of contact with RtlUpcaseUnicodeChar; deriving from it keeps
 * it that way, and means a future correction there propagates here for free.
 *
 * THE "AT MOST TWO" PROPERTY IS AN ASSUMPTION ABOUT A TABLE THIS CODE DOES NOT OWN, so it is
 * RETURNED rather than trusted: wia_casemate_init() reports the largest class it actually saw, and
 * correctness.c fails the build if it is not 2. A future Windows that merged a third unit into a
 * class would otherwise silently produce a search that misses matches -- exactly the kind of quiet
 * wrongness this project has been bitten by before.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern unsigned short wia_upcase[65536];        /* change 210 */
void wia_upcase_init(void);

unsigned short wia_casemate[65536];

static int cm_first[65536];
static int cm_second[65536];
static unsigned char cm_count[65536];

/* Returns the largest case-equivalence class size observed. Idempotent. */
int wia_casemate_init(void)
{
    static int done = 0;
    static int maxsz = 0;
    int i;

    if (done) return maxsz;
    done = 1;

    wia_upcase_init();

    for (i = 0; i < 65536; ++i) { cm_first[i] = -1; cm_second[i] = -1; cm_count[i] = 0; }

    for (i = 0; i < 65536; ++i) {
        int r = wia_upcase[i];
        if (cm_count[r] < 255) ++cm_count[r];
        if (cm_first[r] < 0)       cm_first[r]  = i;
        else if (cm_second[r] < 0) cm_second[r] = i;
    }

    for (i = 0; i < 65536; ++i) if (cm_count[i] > maxsz) maxsz = cm_count[i];

    for (i = 0; i < 65536; ++i) {
        int r = wia_upcase[i];
        int other = (cm_first[r] == i) ? cm_second[r] : cm_first[r];
        wia_casemate[i] = (unsigned short)((other < 0) ? i : other);
    }
    return maxsz;
}
