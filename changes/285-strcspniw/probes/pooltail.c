/* changes/285-strcspniw/probes/pooltail.c
 *
 * Two facts the mutation sweep turned into questions, asked instead of assumed.
 *
 * Two mutants of change 285 survived both gates, and whether each is harmless or a real defect depends
 * on something about change 281's generated tables that had never been written down:
 *
 *   1. A POOL SLOT holds n members for a code unit with n partners, in a sixteen-byte slot, room for
 *      eight. a mutant that walks one member too far reads entry [n]. If that entry is zero the extra
 *      comparison can never fire, because the scalar loop tests for the terminator before it compares,
 *      so a zero string character never reaches the compare. If it is a STALE value from another set,
 *      the mutant is a false-match bug and the gates simply failed to catch it.
 *
 *   2. THE 255 SENTINEL means a code unit's set is stored as a bitmap. A mutant that removes the
 *      terminator test from the scalar bitmap loop survived, which is only harmless if bit 0 is SET
 *      in that bitmap, so that the terminator stops the loop anyway. The ignorables include 0x0000
 *      (change 282's foldnul.c lists it first), so for THAT bitmap it is harmless. But there is more
 *      than one bitmap set: U+D7A2 also carries the sentinel and accepts only 238 code units. If bit 0
 *      is clear there, the mutant runs off the end of the string and the corpus never asked.
 *
 * Both are measured here, over every code unit rather than over an example.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern const unsigned char wia_sci_n[];
extern const unsigned short wia_sci_slot[];
extern const unsigned short wia_sci_pool[];

int main(void)
{
    unsigned c, k;
    long nonzero_tail = 0, checked = 0, worst = 0;
    long sentinels = 0, with_bit0 = 0, without_bit0 = 0;
    unsigned first_without = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }

    printf("== 1. what does a POOL SLOT hold past its n members? ==\n");
    for (c = 1; c < 65536; ++c) {
        unsigned n = wia_sci_n[c];
        const unsigned short* slot;
        if (n == 0 || n == 255) continue;
        slot = wia_sci_pool + (size_t)wia_sci_slot[c] * 8;
        ++checked;
        for (k = n; k < 8; ++k) {
            if (slot[k] != 0) {
                if (nonzero_tail < 6)
                    printf("   U+%04X (n=%u): entry [%u] is U+%04X, NOT zero\n", c, n, k, slot[k]);
                ++nonzero_tail;
                if (k == n) ++worst;      /* the one a one-too-far walk would read */
                break;
            }
        }
    }
    printf("   %ld slots checked, %ld have a non-zero entry past their members,\n"
           "   %ld of those at exactly entry [n] -- the one a one-too-far walk reads\n",
           checked, nonzero_tail, worst);
    /* The deciding question is not whether entry [n] is zero but whether it can ever be a code unit
       that is NOT already accepted. If the padding only ever repeats a member, the extra comparison
       cannot match anything new and a one-too-far walk changes no answer. */
    {
        long outsiders = 0;
        unsigned firstbad = 0, badval = 0;
        for (c = 1; c < 65536; ++c) {
            unsigned n = wia_sci_n[c];
            const unsigned short* slot;
            unsigned tail;
            if (n == 0 || n == 255) continue;
            slot = wia_sci_pool + (size_t)wia_sci_slot[c] * 8;
            tail = slot[n];
            if (tail == 0) continue;                 /* unreachable: the terminator is tested first */
            if (!wia_sci_match(c, tail)) {
                if (!firstbad) { firstbad = c; badval = tail; }
                ++outsiders;
            }
        }
        printf("   of those, %ld hold a code unit that is NOT already a member of the set\n", outsiders);
        if (!outsiders)
            printf("   => entry [n] is always either zero or a REPEAT of a member, so a one-too-far\n"
                   "      walk can only compare against something already accepted: it changes no\n"
                   "      answer, and that mutant is equivalent\n");
        else
            printf("   => U+%04X's entry [n] is U+%04X, which its set does NOT accept, so a\n"
                   "      one-too-far walk is a genuine false-match bug\n", firstbad, badval);
    }

    printf("\n== 2. is bit 0 set in every 255-sentinel bitmap? ==\n");
    for (c = 0; c < 65536; ++c) {
        if (wia_sci_n[c] != 255) continue;
        ++sentinels;
        if (wia_sci_match(c, 0)) ++with_bit0;
        else {
            ++without_bit0;
            if (!first_without) first_without = c;
            if (without_bit0 <= 6)
                printf("   U+%04X carries the sentinel but does NOT accept a NUL\n", c);
        }
    }
    printf("   %ld code units carry the 255 sentinel: %ld accept a NUL, %ld DO NOT\n",
           sentinels, with_bit0, without_bit0);
    if (without_bit0)
        printf("   => the scalar bitmap loop MUST test the terminator itself; U+%04X is a set member\n"
               "      for which the bitmap would not stop it, and the corpus has to contain one\n",
               first_without);
    else
        printf("   => every sentinel bitmap contains a NUL, so the terminator stops the loop anyway\n");

    printf("\n== 3. and how many DISTINCT sentinel sets are there? ==\n");
    {
        static unsigned short rep[64];
        int nrep = 0, i;
        for (c = 0; c < 65536 && nrep < 64; ++c) {
            int fresh = 1;
            if (wia_sci_n[c] != 255) continue;
            for (i = 0; i < nrep; ++i)
                if (wia_sci_match(rep[i], c)) { fresh = 0; break; }
            if (fresh) rep[nrep++] = (unsigned short)c;
        }
        printf("   %d distinct sentinel sets, represented by:", nrep);
        for (i = 0; i < nrep; ++i) {
            long members = 0;
            for (c = 0; c < 65536; ++c) if (wia_sci_match(rep[i], c)) ++members;
            printf("\n     U+%04X  %ld members,  accepts a NUL: %s",
                   rep[i], members, wia_sci_match(rep[i], 0) ? "YES" : "NO");
        }
        printf("\n");
    }
    return 0;
}
