/* changes/236-pathcommonprefixa/probes/pcpa6.c
   THE MODEL, REFINED BY WHAT pcpa5.c ISOLATED, and checked against the live export again.

   pcpa4.c's model failed 3145 times. pcpa5.c took the two failure shapes apart:

   1. THE LENGTH-TWO RETURN IS THREE, AND THE BUFFER DISAGREES WITH IT.

        PathCommonPrefixA("aa", "aa", out)  ->  3,  buffer = 61 61 00

      Two characters and a terminator are written; three is returned. The fourth byte of a poison
      fill is untouched, so it does not invent a backslash, and a 2-character string whose NUL is the
      last readable byte before a PAGE_NOACCESS page does NOT fault, so it does not read past the
      terminator either. THE RETURNED COUNT SIMPLY EXCEEDS THE STRING IT PRODUCED BY ONE. That is a
      defect in the shipped export -- a caller who trusts the return walks one character past the
      terminator of the buffer it was just handed -- and because this project's contract is to be
      indistinguishable from the shipped function, it is reproduced exactly rather than fixed.

      It fires at a common prefix of exactly 2 and at no other length (0, 1, 3, 4, 5, 6, 7, 8 all
      return their own length). The shape is the drive-root fixup: a 2-character prefix is taken for
      "X:" and reported as if it were "X:\" -- and, once again, POSITIONALLY, since "aa" and "zz"
      do it just as "C:" does.

   2. THE WHOLE-COMPONENT SHORTCUT DOES NOT APPLY TO A LONE SEPARATOR.

        pcp("a", "a\")  ->  1     the shorter path is a whole component: uncut
        pcp("\", "\\")  ->  0     ... but not when that component is just a separator

      Over 1093 enumerated prefixes with a separator continuation, 1083 took len(P); the exceptions
      are exactly the nine length-2 prefixes (rule 1 above) and P = "\".

   THE MODEL NOW READS:

       k = length of the fold-equal run
       n = k                                   if both paths end at k
         = k                                   if one ends at k and the other continues with a
                                               separator, UNLESS k == 1 and the path starts with one
         = 0                                   in that exception
         = j                                   otherwise, j = index of the last separator before k
         = 0                                   ... or 0 if there is none, or if j == 1 and the path
                                               starts with a separator
       if (n == 2) n = 3                       the drive-root fixup
       write min(n, strlen(a)) characters and a terminator -- a BOUNDED copy that stops at
       the terminator, which is why the fixup can report 3 while writing only "aa"
       NULL in either path writes NOTHING AT ALL, not even a terminator

   THE BOUND THIS FILE ORIGINALLY MISSED, recorded here rather than quietly fixed. The first
   version of this probe reported 0 mismatches over 3.65 million pairs and the model was STILL
   wrong: when the RESULT reaches MAX_PATH the copy is refused and only a bare terminator is
   written, while the count comes back unchanged. The threshold is exact -- 259 writes 259
   characters and a terminator, which is 260 bytes, and 260 writes nothing but the terminator --
   and the bound is on the RESULT, not the inputs, since 900-character paths whose common prefix
   is 15 copy normally. correctness.c found it within seconds of first running, because it sweeps
   to 600 characters and this file's longest sweep went to 250.

   The lesson is worth more than the rule: AN EXHAUSTIVE CORPUS IS ONLY EXHAUSTIVE OVER THE
   DIMENSION IT ENUMERATES. Every alphabet here was complete and every arrangement was covered, and
   LENGTH was not one of the dimensions being enumerated at all. The sweep below now crosses 260.

   Every constant is measured. This file's job is to find out whether it is still incomplete. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
static PCP pcp;

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

static int model(const char* a, const char* b, char* out)
{
    int n;
    /* NULL leaves the buffer COMPLETELY UNTOUCHED -- not even a terminator. Only a poison
       fill shows that, and it is the opposite of the no-common-prefix case, which DOES
       write one. */
    if (!a || !b) return 0;
    int k = 0;
    while (a[k] && b[k] && FOLD[(unsigned char)a[k]] == FOLD[(unsigned char)b[k]]) ++k;

    if (!a[k] && !b[k])
        n = k;                                          /* the same path */
    else if ((!a[k] && b[k] == '\\') || (!b[k] && a[k] == '\\'))
        n = (k == 1 && a[0] == '\\') ? 0 : k;           /* a whole component, unless it is "\" */
    else {
        int j = -1;
        for (int i = 0; i < k; ++i) if (a[i] == '\\') j = i;
        if (j < 0)                            n = 0;
        else if (j == 1 && a[0] == '\\')      n = 0;    /* the UNC prefix alone is not a prefix */
        else                                  n = j;
    }
    if (n == 2) n = 3;                                  /* the drive-root fixup */
    if (out && n >= MAX_PATH) {
        /* THE BOUND THIS PROBE ORIGINALLY MISSED. See the header. */
        out[0] = 0;
    } else if (out) {
        /* A BOUNDED copy of n characters that STOPS AT THE TERMINATOR -- which is why the fixup
           can report 3 while writing only 2: "aa" has nothing to copy for the third. When the
           first path does have a third character, as "aa\\" does, all three are written. */
        int la = 0; while (a[la]) ++la;
        int wn = n < la ? n : la;
        memcpy(out, a, (size_t)wn); out[wn] = 0;
    }
    return n;
}

#define POISON 0xCD
static char o1[4096], o2[4096];
static long fails = 0;
static int shown = 0;

static void one(const char* a, const char* b, int cmp_buffer)
{
    memset(o1, POISON, 400); memset(o2, POISON, 400);
    int r1 = pcp(a, b, o1);
    int r2 = model(a, b, o2);
    int ok = (r1 == r2);
    if (ok && cmp_buffer) ok = (memcmp(o1, o2, 400) == 0);   /* the WHOLE window, poison included */
    if (!ok) {
        ++fails;
        if (shown < 25) {
            printf("    MISMATCH \"%s\" \"%s\": live %d \"%s\"   model %d \"%s\"\n",
                   a ? a : "(NULL)", b ? b : "(NULL)", r1, o1, r2, o2);
            ++shown;
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    if (!pcp) { printf("cannot resolve PathCommonPrefixA\n"); return 1; }
    build_fold();
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== 1. exhaustive, fold-heavy alphabet { a A backslash colon 0x5E 0x88 } to length 4 ===\n");
    printf("  The WHOLE 400-byte output window is compared, poison included, so a model that writes\n");
    printf("  one byte too many or too few fails here even when the return matches.\n");
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
        printf("    %ld pairs, %ld mismatches\n", pairs, fails);
    }

    printf("\n=== 2. {a, backslash, colon} to length 6, both arguments ===\n");
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

    printf("\n=== 3. every byte value in a realistic path ===\n");
    {
        long before = fails, cases = 0;
        for (int v = 1; v < 256; ++v) {
            char a[64], b[64];
            for (int pos = 0; pos < 8; ++pos) {
                strcpy(a, "C:\\dir\\sub\\file.txt");
                strcpy(b, "C:\\dir\\sub\\file.txt");
                a[3 + pos] = (char)v;
                one(a, b, 1); ++cases;
                b[3 + pos] = (char)FOLD[v];
                one(a, b, 1); ++cases;
                b[3 + pos] = (char)v;
                one(a, b, 1); ++cases;
            }
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 4. long paths, divergence walked across every position, in three shapes ===\n");
    {
        long before = fails, cases = 0;
        static char a[600], b[600];
        for (int shape = 0; shape < 3; ++shape) {
            for (int n = 8; n <= 600; n += 23) {     /* PAST MAX_PATH, unlike the first version */
                for (int i = 0; i < n; ++i) {
                    if (shape == 0) a[i] = (i % 7 == 6) ? '\\' : (char)('a' + i % 23);
                    else if (shape == 1) a[i] = (char)('a' + i % 23);        /* no separator at all */
                    else a[i] = (i < 3) ? "C:\\"[i] : ((i % 5 == 4) ? '\\' : (char)('a' + i % 23));
                    b[i] = a[i];
                }
                a[n] = 0; b[n] = 0;
                one(a, b, 1); ++cases;
                for (int pos = 0; pos < n; ++pos) {
                    char save = b[pos];
                    b[pos] = (char)(save == 'z' ? 'y' : 'z');
                    one(a, b, 1); ++cases;
                    if (save >= 'a' && save <= 'z') { b[pos] = (char)(save - 0x20); one(a, b, 1); ++cases; }
                    b[pos] = save;
                }
                /* and one path a strict prefix of the other, cut at every position */
                for (int cutp = 0; cutp <= n; ++cutp) {
                    char save = b[cutp]; b[cutp] = 0;
                    one(a, b, 1); one(b, a, 1); cases += 2;
                    b[cutp] = save;
                }
            }
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 5. NULL ===\n");
    {
        long before = fails;
        one(NULL, "C:\\a", 1); one("C:\\a", NULL, 1); one(NULL, NULL, 1);
        one("abc", "xyz", 1);        /* the contrast: this one DOES write a terminator */
        printf("    %ld new mismatches\n", fails - before);
    }

    printf("\n=== TOTAL: %ld mismatches ===\n", fails);
    printf("%s\n", fails ? "THE MODEL IS STILL INCOMPLETE -- write no assembly yet"
                         : "the model reproduces the live export exactly, RETURN AND BUFFER, on "
                           "every case tested");
    return fails ? 1 : 0;
}
