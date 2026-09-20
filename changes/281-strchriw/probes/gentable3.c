/* changes/281-strchriw/probes/gentable3.c
 *
 * The complete relation: small match sets inline, large ones deduplicated.
 *
 * probes/gentable2.c enumerated the full relation and settled its shape:
 *
 *     10549933 matching pairs, largest set 3237, 3320 needles with more than 8 partners
 *     asymmetric entries 0, INTRANSITIVE triples 168
 *
 * The intransitivity is why there are no classes and why everything must be indexed by NEEDLE. But
 * gentable2 wrote only a count of 255 for the 3320 big needles and none of their members, and the
 * implementation cannot fall back on a set it does not have.
 *
 * Storing them naively is 3320 x 3237 entries, 21 MB. They do not need storing naively: almost all
 * of those needles are the ignorables, and a needle that matches the same things as another needle
 * can share one set. So this deduplicates by content and writes each DISTINCT large set once.
 *
 * The implementation turns each distinct large set into an 8 KB membership bitmap at init, so the
 * fallback path is one load and one BT per code unit, a scalar loop, but a scalar loop at about a
 * nanosecond per character against the shipped export's forty-three.
 *
 * Writes foldsets.c (small sets, by needle) and foldbig.c (distinct large sets + the index).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);
static F_chr chrI;

static wchar_t hay[65537];
static unsigned short small[65536][8];
static unsigned char  nsmall[65536];
static unsigned char  isbig[65536];

/* the big sets, deduplicated */
#define MAXBIG 64
static unsigned short* bigset[MAXBIG];
static unsigned        bignum[MAXBIG];
static unsigned long long bighash[MAXBIG];
static unsigned        nbig = 0;
static unsigned short  bigof[65536];        /* needle -> 1-based index into bigset, or 0 */

static unsigned short scratch[70000];

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    unsigned c, k;
    LARGE_INTEGER t0, t1, fq;
    FILE* f;
    long total = 0, nrows = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&fq);
    chrI = (F_chr)GetProcAddress(hs, "StrChrIW");
    if (!chrI) { printf("resolve failed\n"); return 1; }

    for (c = 1; c <= 0xFFFF; ++c) hay[c - 1] = (wchar_t)c;
    hay[0xFFFF] = 0;

    /* Needle 0 Is enumerated too, and that is not a formality.
       probes/contract.c measured StrChrIW("abcXYZabc", 0) as NULL and wrote down "the terminator is
       never found" as a contract fact. It is not: that string simply contains no ignorable
       character. In a 275-character random string the live export returns offset 28 for needle 0,
       because NUL has zero collation weight and therefore matches every other zero-weight code
       unit. The strengthened live harness caught it in 940 of 40000 cases. Needle 0 is an ordinary
       needle with an ordinary match set; the only thing special about it is that the scan also
       stops on it. */
    printf("enumerating every needle's full match set...\n");
    QueryPerformanceCounter(&t0);
    for (c = 0; c <= 0xFFFF; ++c) {
        PCWSTR p = hay;
        unsigned n = 0;
        unsigned long long h = 1469598103934665603ull;
        for (;;) {
            p = chrI(p, (WCHAR)c);
            if (!p) break;
            if (n < 70000) scratch[n] = (unsigned short)(p - hay + 1);
            ++n;
            ++p;
            if (!*p) break;
        }
        total += n;
        if (n <= 8) {
            nsmall[c] = (unsigned char)n;
            for (k = 0; k < n; ++k) small[c][k] = scratch[k];
            continue;
        }
        isbig[c] = 1;
        for (k = 0; k < n; ++k) { h ^= scratch[k]; h *= 1099511628211ull; }
        {
            unsigned j, found = 0;
            for (j = 0; j < nbig; ++j)
                if (bighash[j] == h && bignum[j] == n) { bigof[c] = (unsigned short)(j + 1); found = 1; break; }
            if (!found) {
                if (nbig >= MAXBIG) { printf("   too many distinct large sets\n"); return 1; }
                bigset[nbig] = (unsigned short*)malloc(n * sizeof(unsigned short));
                for (k = 0; k < n; ++k) bigset[nbig][k] = scratch[k];
                bignum[nbig] = n;
                bighash[nbig] = h;
                bigof[c] = (unsigned short)(nbig + 1);
                ++nbig;
            }
        }
        if ((c & 0x1FFF) == 0) printf("   ... %u\n", c);
    }
    QueryPerformanceCounter(&t1);
    printf("   done in %.1f s\n", (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart);
    printf("   %ld matching pairs; %u DISTINCT large sets covering the needles with >8 partners\n",
           total, nbig);
    for (k = 0; k < nbig; ++k) printf("     large set %u: %u members\n", k + 1, bignum[k]);

    /* ---- foldsets.c : the small sets, by needle ---- */
    f = fopen("../foldsets.c", "w");
    if (!f) { printf("cannot write ../foldsets.c\n"); return 1; }
    fprintf(f,
"/* changes/281-strchriw/foldsets.c -- GENERATED by probes/gentable3.c. Do not edit by hand.\n"
" *\n"
" * shlwapi!StrChrIW's match relation for every needle with EIGHT OR FEWER partners, taken from the\n"
" * live export by enumerating each needle's whole match set.\n"
" *\n"
" * THE RELATION IS SYMMETRIC BUT NOT TRANSITIVE -- U+D7B0 matches U+D7A2 and U+D7B1 matches\n"
" * U+D7A2, but U+D7B0 does not match U+D7B1 -- so it has NO CLASSES and everything is indexed by\n"
" * NEEDLE. A needle absent from this table and from foldbig.c matches only itself.\n"
" *\n"
" * Rows are (needle, count, partners...).\n"
" */\n"
"const unsigned short wia_sci_sets[] = {\n");
    for (c = 0; c <= 0xFFFF; ++c) {
        if (isbig[c]) continue;
        if (nsmall[c] == 1 && small[c][0] == c) continue;       /* matches only itself */
        fprintf(f, "0x%04X,%u,", c, nsmall[c]);
        for (k = 0; k < nsmall[c]; ++k) fprintf(f, "0x%04X,", small[c][k]);
        fprintf(f, "\n");
        ++nrows;
    }
    fprintf(f, "};\nconst unsigned wia_sci_nsetrows = %ld;\n", nrows);
    fclose(f);
    printf("   wrote ../foldsets.c with %ld needle rows\n", nrows);

    /* ---- foldbig.c : the distinct large sets and which needle uses which ---- */
    f = fopen("../foldbig.c", "w");
    if (!f) { printf("cannot write ../foldbig.c\n"); return 1; }
    fprintf(f,
"/* changes/281-strchriw/foldbig.c -- GENERATED by probes/gentable3.c. Do not edit by hand.\n"
" *\n"
" * The needles with MORE THAN EIGHT partners. There are 3320 of them and only %u DISTINCT sets\n"
" * between them -- almost all are the ignorables headed by U+00AD -- so each distinct set is\n"
" * written once and the needles point at it.\n"
" *\n"
" * tables.c turns each one into an 8 KB membership bitmap at init, so the implementation's\n"
" * fallback path costs one load and one BT per code unit.\n"
" */\n", nbig);
    fprintf(f, "const unsigned wia_sci_nbig = %u;\n", nbig);
    fprintf(f, "const unsigned wia_sci_bigcount[] = {");
    for (k = 0; k < nbig; ++k) fprintf(f, "%u,", bignum[k]);
    fprintf(f, "};\n");
    for (k = 0; k < nbig; ++k) {
        unsigned j;
        fprintf(f, "static const unsigned short big%u[] = {", k);
        for (j = 0; j < bignum[k]; ++j) {
            fprintf(f, "0x%04X,", bigset[k][j]);
            if ((j % 12) == 11) fprintf(f, "\n");
        }
        fprintf(f, "};\n");
    }
    fprintf(f, "const unsigned short* const wia_sci_big[] = {");
    for (k = 0; k < nbig; ++k) fprintf(f, "big%u,", k);
    fprintf(f, "};\n");
    fprintf(f, "/* needle -> 1-based large-set index, 0 if the needle has eight or fewer partners */\n");
    fprintf(f, "const unsigned short wia_sci_bigof[] = {\n");
    {
        long n2 = 0;
        for (c = 0; c <= 0xFFFF; ++c) {
            if (!bigof[c]) continue;
            fprintf(f, "0x%04X,%u,", c, bigof[c]);
            if ((++n2 % 8) == 0) fprintf(f, "\n");
        }
        fprintf(f, "};\nconst unsigned wia_sci_nbigof = %ld;\n", n2);
        printf("   wrote ../foldbig.c: %u distinct large sets, %ld needles pointing at them\n",
               nbig, n2);
    }
    fclose(f);
    return 0;
}
