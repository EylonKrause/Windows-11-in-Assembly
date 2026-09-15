/* changes/237-pathisprefixa/probes/pipa2.c
   WHAT THE LENGTH-TWO DEFECT DOES TO "IS A PREFIX".

   pipa.c established the rule over 87 067 561 pairs with zero disagreements:

       PathIsPrefixA(a, b)  ==  (PathCommonPrefixA(a, b, NULL) == strlen(a))

   and it fires with the length-two defect change 236 documented still inside it: PathCommonPrefixA
   REPORTS 3 for a common prefix of exactly 2. Composing those two facts predicts two results that
   look like nonsense until you trace them, and predictions are worth nothing until measured:

     1. A TWO-CHARACTER PATH IS NOT A PREFIX OF ITSELF. PathIsPrefixA("aa", "aa") should be FALSE,
        because the common prefix is reported as 3 and strlen is 2. pipa.c saw exactly that. Every
        longer path IS a prefix of itself, so this is a hole at exactly one length.

     2. A LONGER PATH CAN BE A "PREFIX" OF A SHORTER ONE. If a is "xy\" and b is "xy", the scan stops
        at k = 2 with b exhausted and a continuing with a separator -- the whole-component shape --
        so the count is 2, the fixup reports 3, and strlen(a) is 3. The rule then says TRUE: a
        three-character path is a prefix of a two-character one.

   This file enumerates both, exhaustively over the lengths where they can occur, and counts them, so
   the assembly can be checked against a rule rather than against a guess. A hole at one length and a
   reversal at one shape are precisely the things a realistic path corpus never contains. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PIP)(LPCSTR, LPCSTR);
typedef int  (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
static PIP pip;
static PCP pcp;

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pip = (PIP)GetProcAddress(hs, "PathIsPrefixA");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    if (!pip || !pcp) { printf("cannot resolve\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== 1. is a path a prefix of ITSELF, by length? ===\n");
    {
        char s[64];
        for (int n = 0; n <= 10; ++n) {
            for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
            s[n] = 0;
            int r = !!pip(s, s);
            printf("  len %2d: IsPrefix(s,s) = %d   CommonPrefix = %d%s\n",
                   n, r, pcp(s, s, NULL), r ? "" : "   <== NOT a prefix of itself");
        }
    }

    printf("\n=== 2. the predicted reversal: a LONGER path as a prefix of a SHORTER one ===\n");
    {
        static const char* V[][2] = {
            { "xy\\",  "xy"   },
            { "ab\\",  "ab"   },
            { "C:\\",  "C:"   },
            { "\\\\\\","\\\\" },
            { "abc\\", "abc"  },
            { "xyz\\", "xy"   },
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i) {
            int r = !!pip(V[i][0], V[i][1]);
            printf("  IsPrefix(\"%s\", \"%s\") = %d   CommonPrefix = %d, strlen(first) = %d%s\n",
                   V[i][0], V[i][1], r, pcp(V[i][0], V[i][1], NULL), (int)strlen(V[i][0]),
                   (r && strlen(V[i][0]) > strlen(V[i][1])) ? "   <== LONGER IS A PREFIX" : "");
        }
    }

    printf("\n=== 3. exhaustive: count both anomalies over {a, b, backslash, colon} to length 6 ===\n");
    {
        static const char AL[4] = { 'a', 'b', '\\', ':' };
        char a[16], b[16];
        long pairs = 0, model_bad = 0;
        long self_false = 0, longer_is_prefix = 0, trues = 0;
        int shown_l = 0;
        for (int la = 0; la <= 6; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 4;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 4]; v /= 4; }
                a[la] = 0;
                if (!pip(a, a)) ++self_false;
                for (int lb = 0; lb <= 6; ++lb) {
                    long cb = 1; for (int i = 0; i < lb; ++i) cb *= 4;
                    for (long kb = 0; kb < cb; ++kb) {
                        long w = kb;
                        for (int i = 0; i < lb; ++i) { b[i] = AL[w % 4]; w /= 4; }
                        b[lb] = 0;
                        int r = !!pip(a, b);
                        int m = (pcp(a, b, NULL) == la);
                        if (r != m) ++model_bad;
                        if (r) ++trues;
                        if (r && la > lb) {
                            ++longer_is_prefix;
                            if (shown_l < 12) {
                                printf("    LONGER IS A PREFIX: \"%s\" (%d) of \"%s\" (%d), "
                                       "CommonPrefix %d\n", a, la, b, lb, pcp(a, b, NULL));
                                ++shown_l;
                            }
                        }
                        ++pairs;
                    }
                }
            }
        }
        printf("\n    %ld pairs, %ld TRUE, %ld disagreements with "
               "\"CommonPrefix == strlen(first)\"\n", pairs, trues, model_bad);
        printf("    %ld strings are NOT a prefix of themselves\n", self_false);
        printf("    %ld pairs where a LONGER path is a prefix of a SHORTER one\n",
               longer_is_prefix);
        /* The closed forms: over a 4-letter alphabet there are 4^2 = 16 strings of length 2, and
           each should fail the self test; and the reversal needs a[2] == backslash with a of
           length 3 and b its first two characters, which is 4*4 = 16 shapes. */
        printf("    (a 4-letter alphabet has 4^2 = %d strings of length 2, and the reversal needs\n"
               "     a of length 3 ending in a separator with b its first two characters, which is\n"
               "     4*4 = %d shapes -- compare those against the counts above)\n", 16, 16);
    }
    return 0;
}
