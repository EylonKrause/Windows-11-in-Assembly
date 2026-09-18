/* changes/283-strrstriw/probes/partners.c
 *
 * HOW MANY PARTNERS DOES EACH CODE UNIT HAVE, AND WHICH COUNTS ACTUALLY OCCUR?
 *
 * The implementation dispatches the first-character filter on that count: zero partners takes a
 * single-broadcast path, up to four takes the four-register path, and anything more takes a WIDE
 * path that bypasses the vector filter entirely. A mutant that moved the threshold from 4 to 200
 * SURVIVED the whole correctness corpus, because the only many-partner needle the corpus ever used
 * was the ignorable set -- which change 281 stores behind a 255 sentinel, so it took the WIDE path
 * either way and the moved threshold changed nothing.
 *
 * To cover the dispatch the corpus needs needles whose first character has 5, 6, ... partners, and
 * this probe says which counts exist and gives a representative code unit for each.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern const unsigned char wia_sci_n[];
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);

int main(void)
{
    static long hist[256];
    static int rep[256];
    int c, i, shown;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("tables disagree with the live export\n"); return 1; }

    for (c = 1; c < 65536; ++c) {
        int n = wia_sci_n[c];
        ++hist[n];
        if (!rep[n]) rep[n] = c;
    }

    printf("partner-count distribution over code units 1..65535\n");
    printf("  count  how many code units   a representative\n");
    for (i = 0, shown = 0; i < 256; ++i) {
        if (!hist[i]) continue;
        printf("  %5d  %18ld   U+%04X\n", i, hist[i], rep[i]);
        ++shown;
    }
    printf("  (%d distinct counts occur)\n\n", shown);

    printf("the code units the corpus can use to drive each dispatch path:\n");
    printf("  0 partners  -> the single-broadcast path\n");
    printf("  1..4        -> the four-register path\n");
    printf("  5..254      -> the WIDE path, via a real count\n");
    printf("  255         -> the WIDE path, via the bitmap sentinel\n\n");

    for (i = 5; i < 255; ++i) {
        if (!hist[i]) continue;
        printf("  count %3d: U+%04X, whose set is {", i, rep[i]);
        {
            int k, first = 1, shown2 = 0;
            for (k = 0; k < 65536 && shown2 < 8; ++k) {
                if (wia_sci_match((unsigned)rep[i], (unsigned)k)) {
                    printf("%sU+%04X", first ? "" : ",", k);
                    first = 0; ++shown2;
                }
            }
            printf("%s}\n", shown2 >= 8 ? ",..." : "");
        }
    }
    return 0;
}
