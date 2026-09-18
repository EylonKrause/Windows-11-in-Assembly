/* changes/285-strcspniw/probes/contract.c
 *
 * THE CONTRACT OF shlwapi!StrCSpnIW, ASKED FROM SCRATCH.
 *
 *     int StrCSpnIW(PCWSTR pszStr, PCWSTR pszSet)
 *
 * It returns a COUNT, not a pointer: the number of leading characters of pszStr that are NOT in
 * pszSet -- equivalently the index of the first character that IS. That makes it a different shape
 * from changes 283 and 284, which searched for a SEQUENCE. Here every character of the string is
 * tested against a SET, and change 281 established that the relation behind this family is a
 * TOLERANCE relation: symmetric, but INTRANSITIVE, with 168 triples and therefore no equivalence
 * classes at all.
 *
 * That has a consequence which has to be checked rather than assumed. "c is in the set S" can only
 * mean "there exists s in S with match(s, c)" -- the union of the match sets of the members. With no
 * classes, that union is not itself a class and cannot be collapsed. A set of k characters can
 * therefore accept far more than k code units, and whether the export really behaves that way is the
 * first question below.
 *
 * THE QUESTIONS THIS FAMILY HAS TAUGHT ME TO ASK, in order. Change 283 shipped two wrong drafts
 * because its corpus could not express a case; change 284's first draft inherited 283's empty-needle
 * answer and was wrong because the two exports genuinely differ. So:
 *
 *   1. is it per character over change 281's relation, and is the set really a UNION?
 *   2. what comes back when nothing matches -- the length?
 *   3. the empty set, the empty string, and NULL arguments;
 *   4. THE VIRTUAL NUL. Changes 283 and 284 both found that past the terminator the string behaves
 *      as an endless run of NULs which are never loaded, and that 3320 code units match a NUL. Here
 *      that would mean a set containing such a code unit stops at the terminator -- indistinguishable
 *      from "no match", since both give the length. But an EMBEDDED NUL is distinguishable, and so is
 *      a set whose member matches a NUL when the string is empty;
 *   5. does an embedded NUL end the scan?
 *   6. the intransitive triple, on the SET side this time: if the set is {X} and the string holds Y
 *      with match(X,Y), and Z with match(X,Z) but not match(Y,Z), all of X's partners must count;
 *   7. and the direction: the relation is symmetric, so match(set, str) and match(str, set) should
 *      agree -- but change 281 stores everything indexed by NEEDLE, so if the export were asymmetric
 *      the tables would have to be consulted the other way round. Worth one measurement.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FSPN)(PCWSTR, PCWSTR);
static FSPN cspn;

#define SHY  0x00AD     /* SOFT HYPHEN  -- ignorable, and matches a NUL */
#define ZWSP 0x200B     /* ZERO WIDTH SPACE -- ignorable, does NOT match a NUL */

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    static wchar_t s[64], set[16];
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    cspn = (FSPN)GetProcAddress(hs, "StrCSpnIW");
    if (!cspn) { printf("no StrCSpnIW\n"); return 2; }

    printf("== StrCSpnIW contract (returns a COUNT) ==\n\n");

    printf("-- 1. the basics, and case insensitivity\n");
    printf("   (\"abcdef\", \"DC\")     -> %d   (2 = index of 'c')\n", cspn(L"abcdef", L"DC"));
    printf("   (\"abcdef\", \"cd\")     -> %d\n", cspn(L"abcdef", L"cd"));
    printf("   (\"ABCDEF\", \"dc\")     -> %d\n", cspn(L"ABCDEF", L"dc"));
    printf("   (\"abcdef\", \"A\")      -> %d   (0 = the very first character)\n",
           cspn(L"abcdef", L"A"));
    printf("   (\"abcdef\", \"F\")      -> %d   (5 = the last)\n", cspn(L"abcdef", L"F"));

    printf("\n-- 2. nothing matches\n");
    printf("   (\"abcdef\", \"xyz\")    -> %d   (6 = the length)\n", cspn(L"abcdef", L"xyz"));
    printf("   (\"abcdef\", \"\")       -> %d   (6 = the length, for an empty set)\n",
           cspn(L"abcdef", L""));

    printf("\n-- 3. degenerate arguments\n");
    printf("   (\"\", \"abc\")          -> %d\n", cspn(L"", L"abc"));
    printf("   (\"\", \"\")             -> %d\n", cspn(L"", L""));
    printf("   (NULL, \"abc\")        -> %d\n", cspn(0, L"abc"));
    printf("   (\"abc\", NULL)        -> %d\n", cspn(L"abc", 0));
    printf("   (NULL, NULL)         -> %d\n", cspn(0, 0));

    printf("\n-- 4. IS THE SET A UNION OF MATCH SETS?  the ignorables are the biggest set there is\n");
    {
        /* change 281: all 3237 ignorables match each other. So a set holding one of them must
           accept a DIFFERENT one appearing in the string. */
        for (k = 0; k < 64; ++k) s[k] = 0;
        s[0] = L'a'; s[1] = L'b'; s[2] = ZWSP; s[3] = L'c'; s[4] = 0;
        set[0] = SHY; set[1] = 0;
        printf("   str {a,b,ZWSP,c},  set {SHY}        -> %d   (2 if the set is a UNION)\n",
               cspn(s, set));
        set[0] = L'q'; set[1] = SHY; set[2] = 0;
        printf("   str {a,b,ZWSP,c},  set {q,SHY}      -> %d   (a two-member set)\n", cspn(s, set));
        set[0] = L'c'; set[1] = 0;
        printf("   str {a,b,ZWSP,c},  set {c}          -> %d   (3, the plain control)\n",
               cspn(s, set));
    }

    printf("\n-- 5. THE VIRTUAL NUL: does a NUL-matching set member stop at the terminator?\n");
    {
        /* With no other match this is indistinguishable from "no match" -- both give the length.
           An EMBEDDED NUL is distinguishable, and so is the empty string. */
        for (k = 0; k < 64; ++k) s[k] = L'W';
        s[0] = L'a'; s[1] = L'b'; s[2] = L'c'; s[3] = 0;          /* 'W' after the terminator */
        set[0] = SHY; set[1] = 0;
        printf("   str \"abc\" + NUL + 'W'..., set {SHY} -> %d   (3 = the length; 3 also means the\n",
               cspn(s, set));
        printf("                                                 terminator matched, so this one\n");
        printf("                                                 cannot tell them apart)\n");
        set[0] = L'W'; set[1] = 0;
        printf("   str \"abc\" + NUL + 'W'..., set {W}   -> %d   (3 if it stops at the terminator,\n",
               cspn(s, set));
        printf("                                                 4+ if it reads past it)\n");
        for (k = 0; k < 64; ++k) s[k] = 0;
        s[0] = 0;
        set[0] = SHY; set[1] = 0;
        printf("   str \"\" , set {SHY}                  -> %d   (0 either way)\n", cspn(s, set));
    }

    printf("\n-- 6. an EMBEDDED NUL\n");
    {
        for (k = 0; k < 64; ++k) s[k] = L'W';
        s[0] = L'a'; s[1] = L'b'; s[2] = 0; s[3] = L'c'; s[4] = L'd'; s[5] = 0;
        printf("   str \"ab\\0cd\", set \"D\"               -> %d   (2 = stopped at the NUL, not 4)\n",
               cspn(s, L"D"));
        set[0] = SHY; set[1] = 0;
        printf("   str \"ab\\0cd\", set {SHY}             -> %d   (2 if the embedded NUL is IN the\n",
               cspn(s, set));
        printf("                                                 set, which would be a real match)\n");
        printf("   str \"ab\\0cd\", set \"B\"               -> %d   (1)\n", cspn(s, L"B"));
    }

    printf("\n-- 7. the intransitive triple, from the SET side\n");
    {
        /* change 281/283: D7A2 matches both D7B0 and D7B1, while D7B0 and D7B1 do not match each
           other. So a set of {D7A2} must accept BOTH, and a set of {D7B0} must NOT accept D7B1. */
        for (k = 0; k < 64; ++k) s[k] = 0;
        s[0] = L'x'; s[1] = 0xD7B0; s[2] = 0;
        set[0] = 0xD7A2; set[1] = 0;
        printf("   str {x,D7B0}, set {D7A2} -> %d   (1: D7A2 accepts D7B0)\n", cspn(s, set));
        s[1] = 0xD7B1;
        printf("   str {x,D7B1}, set {D7A2} -> %d   (1: and D7B1 too)\n", cspn(s, set));
        set[0] = 0xD7B0;
        printf("   str {x,D7B1}, set {D7B0} -> %d   (2: but these two do NOT match each other)\n",
               cspn(s, set));
        s[1] = 0xD7A2;
        printf("   str {x,D7A2}, set {D7B0} -> %d   (1: symmetric, so this direction matches)\n",
               cspn(s, set));
    }

    printf("\n-- 8. a long set, and a set with repeats\n");
    {
        for (k = 0; k < 40; ++k) s[k] = (wchar_t)(L'a' + (k % 20));
        s[40] = 0;
        for (k = 0; k < 12; ++k) set[k] = (wchar_t)(L'T' - k);   /* T,S,R,...,I */
        set[12] = 0;
        printf("   40 chars a..t repeating, set {T..I} -> %d\n", cspn(s, set));
        set[0] = L'c'; set[1] = L'c'; set[2] = L'C'; set[3] = 0;
        printf("   same string, set {c,c,C}            -> %d   (2)\n", cspn(s, set));
    }

    printf("\n-- 9. how long can the answer be?  (the return type is int)\n");
    {
        static wchar_t big[70000];
        for (k = 0; k < 66000; ++k) big[k] = L'a';
        big[66000] = 0;
        printf("   66000 characters of 'a', set \"z\"    -> %d   (66000 if the count is not 16-bit)\n",
               cspn(big, L"z"));
    }

    return 0;
}
