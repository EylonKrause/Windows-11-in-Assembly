/* changes/277-charupperbuffw/tables.c
 *
 * THE TWO CASE TABLES, BUILT BY ASKING THE EXPORTS THEMSELVES.
 *
 * probes/mapping.c established that user32's mapping is a PURE PER-CHARACTER TABLE and nothing more:
 *
 *   * CharUpperBuffW agrees with ntdll!RtlUpcaseUnicodeChar on all 65536 code units, and
 *     CharLowerBuffW with RtlDowncaseUnicodeChar on all 65536;
 *   * it is NOT locale-aware -- it agrees with LCMapStringW(LCMAP_UPPERCASE) under the user
 *     locale, the invariant locale, German AND Turkish, which is the one that would differ if
 *     linguistic casing were involved;
 *   * and it has no context: every code unit maps the same alone as it does inside a run, 0 of
 *     65535. The surrogate pair U+10428 does come out as U+10400, but that falls out of the table
 *     mapping each surrogate on its own -- section 1 covers the surrogates too.
 *
 * That is what makes this change possible where changes 274 and 276 were parked: there is no
 * allocator and no collation behind it, only a lookup this project can own.
 *
 * THE TABLES ARE BUILT FROM THE EXPORTS UNDER TEST, not from ntdll and not from a list. Change 015
 * builds its upcase table from RtlUpcaseUnicodeChar and probes/mapping.c proved the two are
 * identical -- but the function this change has to match is CharUpperBuffW, so that is the one
 * asked. It is the same decision change 269 made about the SDDL aliases and change 210 about its
 * upcase table: a transcribed table is a constant nobody can check by reading it.
 *
 * AND THE ASCII CLAIM IS CHECKED RATHER THAN ASSUMED. The vector path in impl.asm handles a block
 * with no code unit at or above 0x80 by a range subtract -- 'a'..'z' minus 0x20, 'A'..'Z' plus
 * 0x20 -- and everything else by the table. That is only correct if the table AGREES with the range
 * rule below 0x80, which is exactly the kind of thing that is true until it is not: the self-check
 * below walks all 128 and refuses to run if any disagrees.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned short wia_cub_up[65536];
unsigned short wia_cub_dn[65536];

/* Returns 0 on success, non-zero if the tables could not be built or do not satisfy the rule the
   vector path depends on. */
int wia_cub_init(void)
{
    int i;
    for (i = 0; i < 65536; ++i) {
        wchar_t c[2];
        c[0] = (wchar_t)i; c[1] = 0;
        CharUpperBuffW(c, 1);
        wia_cub_up[i] = (unsigned short)c[0];
        c[0] = (wchar_t)i; c[1] = 0;
        CharLowerBuffW(c, 1);
        wia_cub_dn[i] = (unsigned short)c[0];
    }

    /* the rule the vector path rests on, below 0x80 */
    for (i = 0; i < 0x80; ++i) {
        unsigned want_up = (i >= 'a' && i <= 'z') ? (unsigned)(i - 0x20) : (unsigned)i;
        unsigned want_dn = (i >= 'A' && i <= 'Z') ? (unsigned)(i + 0x20) : (unsigned)i;
        if (wia_cub_up[i] != want_up) return 2;
        if (wia_cub_dn[i] != want_dn) return 3;
    }

    /* and a table that came back as the identity everywhere would be a wrong answer that looks
       like a right one -- 'a' has to move */
    if (wia_cub_up['a'] != 'A' || wia_cub_dn['A'] != 'a') return 4;
    return 0;
}
