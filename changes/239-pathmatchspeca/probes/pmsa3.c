/* changes/239-pathmatchspeca/probes/pmsa3.c
   THE MODEL, stated in full and checked against the live export until it does not fail.

   pmsa2.c classified its disagreements against a TEXTBOOK matcher and reported "191 are neither",
   which was not a finding about the function -- it was a baseline that did not yet contain the rules
   pmsa2.c had itself just measured three sections earlier. Classifying failures against a known-wrong
   model tells you how wrong the model is, not what the function does. So this file does what change
   236's pcpa6.c did: it asserts the WHOLE rule as executable C and runs it against shlwapi over
   millions of pairs.

   THE MODEL, every clause measured in pmsa.c or pmsa2.c:

     1. NULL in either argument -> 0. Both, and either alone.

     2. A LITERALLY EMPTY PATTERN MATCHES EVERY SUBJECT. Note this is the whole pattern, not an
        alternative: ";" and " " and ";;" match ONLY the empty subject.

     3. THE PATTERN IS A LIST split on ';'. Any alternative matching is enough. A subject containing
        a semicolon therefore cannot match a pattern containing one: "a;b" vs "a;b" is FALSE.

     4. LEADING SPACES ARE STRIPPED FROM EACH ALTERNATIVE, and trailing ones are NOT: " a" matches
        "a" but "a " does not, and " a" as a SUBJECT does not match " a" as a pattern. Only 0x20 --
        a tab is not stripped.

     5. THE ALTERNATIVE "*.*" MATCHES ANY SUBJECT, including one with no dot at all. It is that exact
        alternative and nothing near it: "*.", ".*", "*.x", "f.*", "*.?", "*.*.*", "..*" and "*..*"
        all fail against "file".

     6. ONE SPARE TRAILING '?' MAY MATCH ZERO CHARACTERS -- exactly one, and only at the very end.
        Q=0 and Q=1 match at every subject length 0..4; Q>=2 never does; and a '?' in the middle or
        at the front never matches zero.

     7. The comparison is CASE-INSENSITIVE over 60 classes -- the pure CP1252 case pairs, with NO
        0x5E/0x88 conflation. That is a THIRD distinct answer from this one DLL: change 236's
        PathCommonPrefixA folds 61 classes INCLUDING that pair, and change 238's PathMakePrettyA maps
        60 values in one direction only. Nothing is inherited.

     8. Otherwise it is an ordinary greedy '*'/'?' matcher, and it backtracks correctly: "aaaaaaaa"
        vs "a*a*a*a*b" is FALSE and "aaaaaaab" is TRUE.

   Clauses 2 and 6 are expressed the way the implementation will have to: an empty ALTERNATIVE needs
   no special case at all, because an empty pattern matched against an empty subject succeeds in the
   ordinary matcher and fails against a non-empty one. Only the whole-pattern-empty case is special.
   And the spare '?' is a single retry with the final '?' removed, which is the cheapest formulation
   and the one that gives Q>=2 the right answer for free. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PMS)(LPCSTR, LPCSTR);
static PMS pms;

/* ---- the fold: the 60 pure CP1252 case pairs, derived in pmsa.c ------------------------------ */
static unsigned char FOLD[256];
static void build_fold(void){
    for (int v = 0; v < 256; ++v) FOLD[v] = (unsigned char)v;
    for (int v = 0x61; v <= 0x7A; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    for (int v = 0xE0; v <= 0xF6; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    for (int v = 0xF8; v <= 0xFE; ++v) FOLD[v] = (unsigned char)(v - 0x20);
    FOLD[0x9A] = 0x8A; FOLD[0x9C] = 0x8C; FOLD[0x9E] = 0x8E;
    FOLD[0xFF] = 0x9F;
    /* deliberately NOT FOLD[0x88] = 0x5E; that is change 236's fold, not this one */
}

/* standard greedy matcher over a pattern of exactly pl characters, case-folded */
static int greedy(const char* s, int sl, const char* p, int pl)
{
    int si = 0, pi = 0, star = -1, ss = 0;
    while (si < sl) {
        if (pi < pl && (p[pi] == '?' || FOLD[(unsigned char)p[pi]] == FOLD[(unsigned char)s[si]])) {
            ++si; ++pi;
        } else if (pi < pl && p[pi] == '*') {
            star = pi++; ss = si;
        } else if (star >= 0) {
            pi = star + 1; si = ++ss;
        } else return 0;
    }
    while (pi < pl && p[pi] == '*') ++pi;
    return pi == pl;
}

/* one alternative: greedy, plus a single retry with a spare trailing '?' dropped */
static int match_alt(const char* s, int sl, const char* p, int pl)
{
    if (greedy(s, sl, p, pl)) return 1;
    if (pl && p[pl-1] == '?' && greedy(s, sl, p, pl - 1)) return 1;
    return 0;
}

static int model(const char* s, const char* p)
{
    if (!s || !p) return 0;
    if (!p[0]) return 1;                       /* the WHOLE pattern empty matches everything */
    int sl = (int)strlen(s);
    const char* a = p;
    for (;;) {
        const char* e = strchr(a, ';');
        int al = e ? (int)(e - a) : (int)strlen(a);
        const char* b = a;
        while (al > 0 && *b == ' ') { ++b; --al; }      /* LEADING spaces only */
        if (al == 3 && b[0] == '*' && b[1] == '.' && b[2] == '*') return 1;
        if (match_alt(s, sl, b, al)) return 1;
        if (!e) break;
        a = e + 1;
    }
    return 0;
}

static long fails = 0;
static int shown = 0;
static void chk(const char* s, const char* p, const char* what)
{
    int r1 = !!pms(s, p);
    int r2 = !!model(s, p);
    if (r1 != r2) {
        ++fails;
        if (shown < 25) {
            printf("    MISMATCH %s: subject \"%s\" pattern \"%s\" -> live %d, model %d\n",
                   what, s ? s : "(NULL)", p ? p : "(NULL)", r1, r2);
            ++shown;
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pms = (PMS)GetProcAddress(hs, "PathMatchSpecA");
    if (!pms) { printf("cannot resolve PathMatchSpecA\n"); return 1; }
    build_fold();
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== 1. exhaustive over {a, b, *, ?, .} patterns x {a, b, .} subjects, both to length 4 ===\n");
    {
        static const char AL[5] = { 'a', 'b', '*', '?', '.' };
        static const char SL[3] = { 'a', 'b', '.' };
        char p[8], s[8];
        long pairs = 0;
        for (int lp = 0; lp <= 4; ++lp) {
            long cp = 1; for (int i = 0; i < lp; ++i) cp *= 5;
            for (long kp = 0; kp < cp; ++kp) {
                long v = kp;
                for (int i = 0; i < lp; ++i) { p[i] = AL[v % 5]; v /= 5; }
                p[lp] = 0;
                for (int ls = 0; ls <= 4; ++ls) {
                    long cs = 1; for (int i = 0; i < ls; ++i) cs *= 3;
                    for (long ks = 0; ks < cs; ++ks) {
                        long w = ks;
                        for (int i = 0; i < ls; ++i) { s[i] = SL[w % 3]; w /= 3; }
                        s[ls] = 0;
                        chk(s, p, "exhaustive 1"); ++pairs;
                    }
                }
            }
        }
        printf("    %ld pairs, %ld mismatches\n", pairs, fails);
    }

    printf("\n=== 2. the SEPARATOR alphabet: {a, *, ;, space} patterns x {a, ;, space} subjects ===\n");
    printf("  This is the one that reaches the list rule and the leading-space strip together.\n");
    {
        static const char AL[4] = { 'a', '*', ';', ' ' };
        static const char SL[3] = { 'a', ';', ' ' };
        char p[8], s[8];
        long pairs = 0, before = fails;
        for (int lp = 0; lp <= 5; ++lp) {
            long cp = 1; for (int i = 0; i < lp; ++i) cp *= 4;
            for (long kp = 0; kp < cp; ++kp) {
                long v = kp;
                for (int i = 0; i < lp; ++i) { p[i] = AL[v % 4]; v /= 4; }
                p[lp] = 0;
                for (int ls = 0; ls <= 4; ++ls) {
                    long cs = 1; for (int i = 0; i < ls; ++i) cs *= 3;
                    for (long ks = 0; ks < cs; ++ks) {
                        long w = ks;
                        for (int i = 0; i < ls; ++i) { s[i] = SL[w % 3]; w /= 3; }
                        s[ls] = 0;
                        chk(s, p, "exhaustive 2"); ++pairs;
                    }
                }
            }
        }
        printf("    %ld pairs, %ld new mismatches\n", pairs, fails - before);
    }

    printf("\n=== 3. every byte value, as a subject character and as a pattern character ===\n");
    {
        long before = fails, cases = 0;
        for (int v = 1; v < 256; ++v) {
            char s[8], p[8];
            s[0] = (char)v; s[1] = 0;
            for (int w = 1; w < 256; ++w) {
                p[0] = (char)w; p[1] = 0;
                chk(s, p, "byte x byte"); ++cases;
            }
            /* and inside a longer subject, past the first block */
            char ls[80], lp[80];
            for (int i = 0; i < 70; ++i) ls[i] = 'a';
            ls[45] = (char)v; ls[70] = 0;
            strcpy(lp, "*"); lp[1] = (char)FOLD[v]; lp[2] = '*'; lp[3] = 0;
            chk(ls, lp, "byte at offset 45"); ++cases;
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 4. the trailing-'?' rule at every length, and the '*.*' alternative in a list ===\n");
    {
        long before = fails, cases = 0;
        char s[32], p[64];
        for (int S = 0; S <= 8; ++S) {
            for (int i = 0; i < S; ++i) s[i] = (char)('a' + i);
            s[S] = 0;
            for (int Q = 0; Q <= 4; ++Q) {
                for (int L = 0; L <= S; ++L) {
                    int k = 0;
                    for (int i = 0; i < L; ++i) p[k++] = (char)('a' + i);
                    for (int i = 0; i < Q; ++i) p[k++] = '?';
                    p[k] = 0;
                    chk(s, p, "trailing ?"); ++cases;
                    /* the same with a '*' in front */
                    k = 0; p[k++] = '*';
                    for (int i = 0; i < L; ++i) p[k++] = (char)('a' + i);
                    for (int i = 0; i < Q; ++i) p[k++] = '?';
                    p[k] = 0;
                    chk(s, p, "star + trailing ?"); ++cases;
                }
            }
        }
        static const char* LISTS[] = { "*.*", "x;*.*", "*.*;x", " *.*", "*.* ", "*.*;*.*",
                                       "x; *.*", "*.**", "**.*", 0 };
        static const char* SUBS[]  = { "file", "file.txt", "", "a.b.c", "dir\\f", ";", " ", 0 };
        for (int i = 0; LISTS[i]; ++i)
            for (int j = 0; SUBS[j]; ++j) { chk(SUBS[j], LISTS[i], "*.* in a list"); ++cases; }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 5. realistic paths and patterns, including the backtracking shapes ===\n");
    {
        long before = fails, cases = 0;
        static const char* SUBS[] = {
            "file.txt", "FILE.TXT", "C:\\dir\\file.txt", "a.b.c.d", "noext",
            "aaaaaaaa", "aaaaaaab", "xaybzc", "abab", "a b", " a", "a ", "", "a;b",
            "\xE0\xC0z", "\x8A\x9A", 0
        };
        static const char* PATS[] = {
            "*.txt", "*.TXT", "*", "?*", "*?", "a*a*a*a*b", "*a*b*c", "*a*b*c*d",
            "*.txt;*.exe", "*.exe;*.txt", "C:\\*", "*\\*.txt", "?????", "*.*", "",
            "*\xE0*", "*\xC0*", "\x8A?", 0
        };
        for (int i = 0; SUBS[i]; ++i)
            for (int j = 0; PATS[j]; ++j) { chk(SUBS[i], PATS[j], "realistic"); ++cases; }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 6. NULL ===\n");
    {
        long before = fails;
        chk(NULL, "*", "NULL subject");
        chk("a", NULL, "NULL pattern");
        chk(NULL, NULL, "both NULL");
        printf("    %ld new mismatches\n", fails - before);
    }

    printf("\n=== TOTAL: %ld mismatches ===\n", fails);
    printf("%s\n", fails ? "THE MODEL IS INCOMPLETE -- write no assembly yet"
                         : "the model reproduces the live export on every case tested");
    return fails ? 1 : 0;
}
