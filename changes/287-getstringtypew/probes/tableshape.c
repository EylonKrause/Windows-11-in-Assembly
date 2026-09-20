/* changes/287-getstringtypew/probes/tableshape.c
 *
 * How big does the table actually have to be?
 *
 * probes/contract.c established that all three GetStringTypeW info types are pure, context-free,
 * locale-invariant functions of the code unit. So the implementation is a lookup -- but a flat table is
 * 65536 WORDs, 128 KB per info type and 384 KB for all three, which does not fit L2 and would thrash it
 * on non-ASCII text. That would be a slow implementation of a fast idea.
 *
 * Unicode tables are almost always heavily redundant, so this probe measures the redundancy rather than
 * guessing at it, and it measures the three things a design decision actually needs:
 *
 *   1. How many distinct values each type takes. If it is small, a 65536-entry byte index into a short
 *      value table halves the size at the cost of a second dependent load.
 *   2. How many distinct 256-ENTRY pages there are, indexing by high byte. This is the classic two-level
 *      Unicode layout: a 256-entry directory of page numbers, then the deduplicated pages. If most
 *      pages are identical -- and for a classification table most of them are usually all-zero or all
 *      one value -- this collapses the table to a few kilobytes, which fits L1 and makes the non-ASCII
 *      case fast rather than merely correct.
 *   3. Whether the ASCII range is special. Real text is overwhelmingly below U+0100; if that range has
 *      few distinct values, a vector fast path can handle it without touching the big table at all.
 *
 * It also prints the full distinct-value list, because those values are the contract: they go into the
 * generated table and the correctness gate re-checks every one of them against the live export.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *FGST)(DWORD, LPCWCH, int, LPWORD);
static FGST gst;

static WORD tab[3][65536];
static const char* TNAME[3] = { "CT_CTYPE1", "CT_CTYPE2", "CT_CTYPE3" };
static const DWORD TKIND[3] = { 1, 2, 4 };

static void build(int t)
{
    unsigned c;
    WCHAR one[2];
    WORD out[2];
    for (c = 1; c < 65536; ++c) {
        one[0] = (WCHAR)c; one[1] = 0;
        out[0] = 0;
        gst(TKIND[t], one, 1, out);
        tab[t][c] = out[0];
    }
    tab[t][0] = 0;
    {   /* U+0000 is classified too -- ask with an explicit count */
        WCHAR z[2]; WORD o[2];
        z[0] = 0; z[1] = 0; o[0] = 0;
        if (gst(TKIND[t], z, 1, o)) tab[t][0] = o[0];
    }
}

int main(void)
{
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    int t;

    setvbuf(stdout, NULL, _IONBF, 0);
    gst = (FGST)GetProcAddress(kb, "GetStringTypeW");
    if (!gst) gst = (FGST)GetProcAddress(k32, "GetStringTypeW");
    if (!gst) { printf("no GetStringTypeW\n"); return 2; }

    printf("== the shape of the three GetStringTypeW tables ==\n");
    printf("   a flat table is 65536 WORDs = 128 KB per type; the question is how much of that is\n");
    printf("   redundant, because a table that does not fit L2 is a slow implementation of a fast idea\n\n");

    for (t = 0; t < 3; ++t) {
        static WORD vals[4096];
        int nvals = 0;
        long i, j;
        long zero_pages = 0, uniform_pages = 0, distinct_pages = 0;
        static int pagerep[256];

        build(t);

        /* 1. distinct values */
        for (i = 0; i < 65536; ++i) {
            int found = 0;
            for (j = 0; j < nvals; ++j) if (vals[j] == tab[t][i]) { found = 1; break; }
            if (!found && nvals < 4096) vals[nvals++] = tab[t][i];
        }

        /* 2. distinct 256-entry pages, by high byte */
        for (i = 0; i < 256; ++i) {
            int dup = -1;
            for (j = 0; j < i; ++j) {
                int same = 1, k;
                for (k = 0; k < 256; ++k)
                    if (tab[t][i * 256 + k] != tab[t][j * 256 + k]) { same = 0; break; }
                if (same) { dup = (int)j; break; }
            }
            pagerep[i] = (dup >= 0) ? dup : (int)i;
            if (dup < 0) ++distinct_pages;
            {
                int k, allzero = 1, uniform = 1;
                for (k = 0; k < 256; ++k) {
                    if (tab[t][i * 256 + k] != 0) allzero = 0;
                    if (tab[t][i * 256 + k] != tab[t][i * 256]) uniform = 0;
                }
                if (allzero) ++zero_pages;
                else if (uniform) ++uniform_pages;
            }
        }

        printf("-- %s\n", TNAME[t]);
        printf("   distinct values                : %d\n", nvals);
        printf("   distinct 256-entry pages       : %ld of 256\n", distinct_pages);
        printf("   all-zero pages                 : %ld\n", zero_pages);
        printf("   uniform but non-zero pages     : %ld\n", uniform_pages);
        printf("   two-level size                 : 256 directory bytes + %ld x 512 = %ld bytes\n",
               distinct_pages, 256 + distinct_pages * 512);
        printf("   flat size                      : 131072 bytes  (%.1fx larger)\n",
               131072.0 / (double)(256 + distinct_pages * 512));
        {
            int k, nlow = 0;
            static WORD lowvals[512];
            for (k = 0; k < 256; ++k) {
                int found = 0, q;
                for (q = 0; q < nlow; ++q) if (lowvals[q] == tab[t][k]) { found = 1; break; }
                if (!found) lowvals[nlow++] = tab[t][k];
            }
            printf("   distinct values in U+0000..U+00FF : %d   (the range real text lives in)\n", nlow);
        }
        if (nvals <= 64) {
            int k;
            printf("   the values themselves          :");
            for (k = 0; k < nvals; ++k) {
                if (k % 8 == 0) printf("\n     ");
                printf(" 0x%04X", vals[k]);
            }
            printf("\n");
        }
        printf("\n");
    }

    printf("== and how many code units share the SAME value as their neighbour, i.e. run length ==\n");
    for (t = 0; t < 3; ++t) {
        long runs = 1, i;
        for (i = 1; i < 65536; ++i) if (tab[t][i] != tab[t][i - 1]) ++runs;
        printf("   %s: %ld runs over 65536 units, mean run %.1f\n",
               TNAME[t], runs, 65536.0 / (double)runs);
    }
    return 0;
}
