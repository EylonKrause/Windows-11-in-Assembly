/* changes/236-pathcommonprefixa/probes/pcpa4.c
   THE MODEL, stated in full and checked against the live export until it does not fail.

   The three probes before this one each answered one question. This one asserts the WHOLE rule as
   executable C and runs it against shlwapi over millions of pairs. Nothing here is inferred: every
   constant below was measured in pcpa.c, pcpa2.c or pcpa3.c, and the point of this file is to find
   out what the measurements MISSED.

   THE MODEL.

     1. NULL in either path -> 0.

     2. Walk both paths together while the characters are FOLD-EQUAL. The fold, derived by
        enumeration in pcpa2.c and NOT taken from any case-mapping API:

            0x61..0x7A, 0xE0..0xF6, 0xF8..0xFE   ->  -0x20
            0x88 -> 0x5E     0x9A -> 0x8A     0x9C -> 0x8C
            0x9E -> 0x8E     0xFF -> 0x9F
            everything else maps to itself

        0x88 -> 0x5E is not a case pair. It is the same conflation that made StrStrA unconvertible,
        but pcpa2.c proved it is STRICTLY PAIRWISE here: 16 387 064 combinations produced no
        expansion, and no byte is ignorable, so one character always matches exactly one.

     3. Let k be the length of that fold-equal run. The answer is k ITSELF -- no truncation -- in
        exactly two shapes:
            both paths end at k (they are the same path), or
            one path ends at k and the other continues with a separator (k is a whole component).

     4. Otherwise the answer is trunc(k), the prefix cut back to a path boundary. pcpa3.c enumerated
        trunc over all 9841 strings of {a, backslash, colon} to length 8, and exactly three rules
        account for every one of them:

            j = index of the last separator in the first k characters, or -1
            j <  0                       ->  0
            j == 2                       ->  3     the separator is KEPT
            j == 1 and the path[0] is a separator -> 0
            otherwise                    ->  j     the separator is dropped

        The second rule is POSITIONAL, not semantic: "aa\" and "::\" keep their separator exactly as
        "C:\" does, because the implementation tests the OFFSET and never looks for a drive letter.
        The counts confirm the rules are complete rather than approximate -- 567 strings keep the
        separator and 9*(1+2+4+8+16+32) = 567; 127 collapse to zero and 1+2+4+8+16+32+64 = 127.

     5. The output buffer is optional, is ALWAYS written when present (0 of 15 256 836 pairs left it
        untouched), and is NUL-terminated at exactly the returned length.

   WHAT THIS FILE IS LOOKING FOR. pcpa3.c found NINE identical pairs out of 3280 that did not return
   their full length, and did not say which. Whatever they are, they are a rule this model does not
   have yet, and they will show up here as mismatches. The file also settles which of the two paths
   is COPIED into the output buffer, which matters as soon as the two differ in case. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
static PCP pcp;

/* ---- the fold, exactly as pcpa2.c derived it ------------------------------------------------ */
static unsigned char FOLD[256];
static void build_fold(void){
    for (int v = 0; v < 256; ++v) FOLD[v] = (unsigned char)v;
    for (int v = 0x61; v <= 0x7A; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    for (int v = 0xE0; v <= 0xF6; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    for (int v = 0xF8; v <= 0xFE; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    FOLD[0x88] = 0x5E;
    FOLD[0x9A] = 0x8A; FOLD[0x9C] = 0x8C; FOLD[0x9E] = 0x8E;
    FOLD[0xFF] = 0x9F;
}

static int trunc_k(const char* a, int k)
{
    int j = -1;
    for (int i = 0; i < k; ++i) if (a[i] == '\\') j = i;
    if (j < 0) return 0;
    if (j == 2) return 3;
    if (j == 1 && a[0] == '\\') return 0;
    return j;
}

static int model(const char* a, const char* b, char* out)
{
    int n;
    if (!a || !b) { if (out) out[0] = 0; return 0; }
    int k = 0;
    while (a[k] && b[k] && FOLD[(unsigned char)a[k]] == FOLD[(unsigned char)b[k]]) ++k;
    if (!a[k] && !b[k])                       n = k;
    else if (!a[k] && b[k] == '\\')           n = k;
    else if (!b[k] && a[k] == '\\')           n = k;
    else                                      n = trunc_k(a, k);
    if (out) { memcpy(out, a, (size_t)n); out[n] = 0; }
    return n;
}

#define POISON 0xCD
static char o1[4096], o2[4096];
static long fails = 0;
static int shown = 0;

static int one(const char* a, const char* b, int cmp_buffer)
{
    memset(o1, POISON, 300); memset(o2, POISON, 300);
    int r1 = pcp(a, b, o1);
    int r2 = model(a, b, o2);
    int ok = (r1 == r2);
    if (ok && cmp_buffer) ok = (memcmp(o1, o2, (size_t)r1 + 1) == 0);
    if (!ok) {
        ++fails;
        if (shown < 20) {
            printf("    MISMATCH \"%s\" \"%s\": live %d \"%s\"   model %d \"%s\"\n",
                   a, b, r1, o1, r2, o2);
            ++shown;
        }
    }
    return ok;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    if (!pcp) { printf("cannot resolve PathCommonPrefixA\n"); return 1; }
    build_fold();
    printf("GetACP() = %u\n\n", GetACP());

    /* ---- 0. WHICH path is copied into the buffer? ------------------------------------------ */
    printf("=== 0. which path is copied into the output buffer? ===\n");
    {
        memset(o1, POISON, 64);
        int r = pcp("C:\\AAA\\b", "c:\\aaa\\c", o1);
        printf("  pcp(\"C:\\\\AAA\\\\b\", \"c:\\\\aaa\\\\c\") -> %d  buffer \"%s\"\n", r, o1);
        printf("  => the %s path is the one copied\n",
               strncmp(o1, "C:\\AAA", 6) == 0 ? "FIRST" :
               strncmp(o1, "c:\\aaa", 6) == 0 ? "SECOND" : "??? neither");
        memset(o1, POISON, 64);
        r = pcp("c:\\aaa\\b", "C:\\AAA\\c", o1);
        printf("  arguments swapped                      -> %d  buffer \"%s\"\n", r, o1);
    }

    /* ---- 1. the nine identical pairs pcpa3.c could not name --------------------------------- */
    printf("\n=== 1. the identical pairs that do NOT return their own length ===\n");
    {
        static const char AL[3] = { 'a', '\\', ':' };
        char a[16];
        long total = 0, bad = 0;
        for (int la = 0; la <= 7; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 3;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 3]; v /= 3; }
                a[la] = 0;
                int r = pcp(a, a, NULL);
                if (r != la) { printf("    \"%s\" (len %d) -> %d\n", a, la, r); ++bad; }
                ++total;
            }
        }
        printf("    %ld of %ld identical pairs did not return their own length\n", bad, total);
    }

    /* ---- 2. the model, over a fold-heavy alphabet ------------------------------------------- */
    printf("\n=== 2. the model vs the live export: exhaustive, fold-heavy alphabet ===\n");
    printf("  alphabet { 'a', 'A', backslash, colon, 0x5E, 0x88 } -- both a case pair AND the\n");
    printf("  0x5E/0x88 conflation, so a model that folds only ASCII fails here.\n");
    {
        static const char AL[6] = { 'a', 'A', '\\', ':', 0x5E, (char)0x88 };
        char a[16], b[16];
        long pairs = 0;
        for (int la = 0; la <= 4; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 6;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 6]; v /= 6; }
                a[la] = 0;
                for (int lb = 0; lb <= 4; ++lb) {
                    long cb = 1; for (int i = 0; i < lb; ++i) cb *= 6;
                    for (long kb = 0; kb < cb; ++kb) {
                        long w = kb;
                        for (int i = 0; i < lb; ++i) { b[i] = AL[w % 6]; w /= 6; }
                        b[lb] = 0;
                        one(a, b, 1);
                        ++pairs;
                    }
                }
            }
        }
        printf("    %ld pairs, %ld mismatches so far\n", pairs, fails);
    }

    /* ---- 3. the model, over a path-shaped alphabet to greater length ------------------------ */
    printf("\n=== 3. the model vs the live export: {a, backslash, colon} to length 6 ===\n");
    {
        static const char AL[3] = { 'a', '\\', ':' };
        char a[16], b[16];
        long pairs = 0, before = fails;
        for (int la = 0; la <= 6; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 3;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 3]; v /= 3; }
                a[la] = 0;
                for (int lb = 0; lb <= 6; ++lb) {
                    long cb = 1; for (int i = 0; i < lb; ++i) cb *= 3;
                    for (long kb = 0; kb < cb; ++kb) {
                        long w = kb;
                        for (int i = 0; i < lb; ++i) { b[i] = AL[w % 3]; w /= 3; }
                        b[lb] = 0;
                        one(a, b, 1);
                        ++pairs;
                    }
                }
            }
        }
        printf("    %ld pairs, %ld new mismatches\n", pairs, fails - before);
    }

    /* ---- 4. ALL 255 byte values, at several positions, against their fold partner ----------- */
    printf("\n=== 4. every byte value in a realistic path, both arguments ===\n");
    {
        long before = fails, cases = 0;
        for (int v = 1; v < 256; ++v) {
            char a[64], b[64];
            for (int pos = 0; pos < 6; ++pos) {
                strcpy(a, "C:\\dir\\file.txt");
                strcpy(b, "C:\\dir\\file.txt");
                a[3 + pos] = (char)v;
                one(a, b, 1); ++cases;
                b[3 + pos] = (char)FOLD[v];
                one(a, b, 1); ++cases;
                /* and with the byte in both, so only the fold class matters */
                b[3 + pos] = (char)v;
                one(a, b, 1); ++cases;
            }
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    /* ---- 5. long paths, and the buffer contents ---------------------------------------------- */
    printf("\n=== 5. long paths with the divergence walked across every position ===\n");
    {
        long before = fails, cases = 0;
        static char a[600], b[600];
        for (int n = 16; n <= 300; n += 17) {
            for (int i = 0; i < n; ++i) {
                a[i] = (i % 7 == 6) ? '\\' : (char)('a' + i % 23);
                b[i] = a[i];
            }
            a[n] = 0; b[n] = 0;
            one(a, b, 1); ++cases;
            for (int pos = 0; pos < n; ++pos) {
                char save = b[pos];
                b[pos] = (char)(save == 'z' ? 'y' : 'z');
                one(a, b, 1); ++cases;
                b[pos] = (char)(save >= 'a' && save <= 'z' ? save - 0x20 : save);  /* case flip */
                one(a, b, 1); ++cases;
                b[pos] = save;
            }
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    /* ---- 6. NULL ----------------------------------------------------------------------------- */
    printf("\n=== 6. NULL ===\n");
    {
        long before = fails;
        one(NULL, "C:\\a", 0);
        one("C:\\a", NULL, 0);
        one(NULL, NULL, 0);
        printf("    %ld new mismatches\n", fails - before);
    }

    printf("\n=== TOTAL: %ld mismatches ===\n", fails);
    printf("%s\n", fails ? "THE MODEL IS INCOMPLETE -- keep probing, write no assembly yet"
                         : "the model reproduces the live export exactly on every case tested");
    return fails ? 1 : 0;
}
