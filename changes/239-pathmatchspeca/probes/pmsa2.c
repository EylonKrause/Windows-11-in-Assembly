/* changes/239-pathmatchspeca/probes/pmsa2.c
   The five quirks in PathMatchSpecA that a from-scratch wildcard matcher gets wrong.

   pmsa.c settled the easy half. Only '*' and '?' are metacharacters, ';' separates alternatives, the
   match is case-insensitive over exactly 60 classes -- the pure CP1252 case pairs, with NO 0x5E/0x88
   conflation, a THIRD distinct answer from the same DLL after change 236's 61 classes and change
   238's 60-value case map -- backtracking behaves like a correct greedy matcher on every shape that
   breaks a naive one, NULL returns 0 in all three positions, and nothing overreads.

   What it also turned up is five behaviours that no standard wildcard grammar has, each of which
   would make a textbook matcher wrong:

     1. A TRAILING '?' CAN MATCH ZERO CHARACTERS.  "ab" vs "ab?" is TRUE, but "ab" vs "ab??" is
        FALSE. One extra '?' is tolerated and two are not, which is not a rule any ordinary matcher
        has, and "exactly one" is a guess until it is swept.

     2. '.' CAN MATCH NOTHING.  "file" vs "*.*" is TRUE -- a subject with no dot at all matches a
        pattern demanding one. That is DOS_DOT behaviour and it has to be bounded: does a '.' match
        nothing anywhere, or only at the end?

     3. THE EMPTY PATTERN MATCHES EVERYTHING.  "a" vs "" is TRUE.

     4. ... BUT ";" MATCHES NOTHING.  "file.txt" vs ";" is FALSE, even though splitting it at the
        semicolon yields two empty alternatives and an empty pattern matches everything. So the
        empty-pattern rule is not a rule about alternatives.

     5. A LONE SPACE MATCHES THE EMPTY SUBJECT.  "" vs " " is TRUE, which suggests patterns are
        trimmed -- and if they are, the trimming has to be measured, not assumed.

   Each is swept here rather than spot-checked, because the whole point of the exercise is that this
   contract is a grammar and grammars are exactly where spot checks give false confidence. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PMS)(LPCSTR, LPCSTR);
static PMS pms;
static int m(const char* s, const char* p){ return !!pms(s, p); }

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pms = (PMS)GetProcAddress(hs, "PathMatchSpecA");
    if (!pms) { printf("cannot resolve PathMatchSpecA\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== 1. HOW MANY trailing '?' may match zero characters? ===\n");
    printf("  Subject of length S, pattern of S literal characters followed by Q question marks.\n");
    printf("  A textbook matcher says TRUE only when Q == 0.\n");
    {
        for (int S = 0; S <= 4; ++S) {
            char s[16], p[32];
            for (int i = 0; i < S; ++i) s[i] = 'a';
            s[S] = 0;
            printf("  subject %d chars:", S);
            for (int Q = 0; Q <= 5; ++Q) {
                int k = 0;
                for (int i = 0; i < S; ++i) p[k++] = 'a';
                for (int i = 0; i < Q; ++i) p[k++] = '?';
                p[k] = 0;
                printf("  Q=%d:%d", Q, m(s, p));
            }
            printf("\n");
        }
        printf("  and with the question marks NOT at the end:\n");
        printf("    \"ab\"  vs \"a?b\"  -> %d   (one ? in the middle, nothing spare)\n", m("ab", "a?b"));
        printf("    \"ab\"  vs \"?ab\"  -> %d   (one ? at the FRONT)\n", m("ab", "?ab"));
        printf("    \"ab\"  vs \"a??b\" -> %d\n", m("ab", "a??b"));
        printf("    \"abc\" vs \"a?c?\" -> %d   (a spare ? at the end, one consumed in the middle)\n",
               m("abc", "a?c?"));
    }

    printf("\n=== 2. WHEN can '.' match nothing? ===\n");
    {
        static const char* V[][2] = {
            { "file",     "*.*"   }, { "file",     "*."    }, { "file",     ".*"    },
            { "file",     "file." }, { "file",     "file.*"}, { "file",     "*.x"   },
            { "file",     "f.*"   }, { "file",     "*.?"   }, { "file.",    "*.*"   },
            { "file.txt", "*.*"   }, { "a.b.c",    "*.*"   }, { "file",     "*.*.*" },
            { "file",     "..*"   }, { "file",     "*..*"  }, { "dir\\f",   "*.*"   },
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i)
            printf("  %-10s vs %-8s -> %d\n", V[i][0], V[i][1], m(V[i][0], V[i][1]));
    }

    printf("\n=== 3/4. the EMPTY pattern, and patterns made only of separators ===\n");
    {
        static const char* PAT[] = { "", ";", ";;", " ", "  ", " ;", "; ", " ; ", "*", "?", 0 };
        static const char* SUB[] = { "", "a", "ab", "file.txt", 0 };
        printf("  pattern \\ subject :");
        for (int j = 0; SUB[j]; ++j) printf(" %-10s", SUB[j][0] ? SUB[j] : "(empty)");
        printf("\n");
        for (int i = 0; PAT[i]; ++i) {
            printf("  %-18s:", PAT[i][0] ? PAT[i] : "(empty)");
            for (int j = 0; SUB[j]; ++j) printf(" %-10d", m(SUB[j], PAT[i]));
            printf("\n");
        }
    }

    printf("\n=== 5. ARE PATTERNS TRIMMED, and on which side? ===\n");
    {
        static const char* V[][2] = {
            { "a",    " a"   }, { "a",    "a "   }, { "a",    " a "  },
            { "a",    "  a"  }, { "a",    "a  "  },
            { " a",   " a"   }, { "a ",   "a "   }, { " a",   "a"    }, { "a ",   "a"    },
            { "a b",  "a b"  }, { "a b",  "a*b"  },
            { "a",    "\ta"  }, { "a",    "a\t"  },
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i)
            printf("  subject \"%s\" vs pattern \"%s\" -> %d\n", V[i][0], V[i][1],
                   m(V[i][0], V[i][1]));
        printf("  and inside a list:\n");
        printf("    \"a\" vs \"*.exe; a\"  -> %d\n", m("a", "*.exe; a"));
        printf("    \"a\" vs \"*.exe ;a\"  -> %d\n", m("a", "*.exe ;a"));
    }

    printf("\n=== 6. the SEMICOLON list, enumerated ===\n");
    printf("  Does an alternative that is itself empty match anything? Does a subject containing a\n");
    printf("  semicolon ever match a pattern containing one?\n");
    {
        static const char* V[][2] = {
            { "a",   "a;b"    }, { "b",   "a;b"    }, { "c",   "a;b"    },
            { "a",   "a;;b"   }, { "a",   ";;"     }, { "",    "a;"     },
            { "",    ";a"     }, { "",    "a;b"    },
            { "a;b", "a;b"    }, { "a;b", "*"      }, { "a;b", "a*b"    },
            { "a;b", "a\\;b"  },
            { 0, 0 }
        };
        for (int i = 0; V[i][0] || V[i][1]; ++i)
            printf("  %-6s vs %-8s -> %d\n", V[i][0][0] ? V[i][0] : "(empty)", V[i][1],
                   m(V[i][0], V[i][1]));
    }

    printf("\n=== 7. EXHAUSTIVE cross-check against a TEXTBOOK matcher ===\n");
    printf("  A standard greedy '*'/'?' matcher, case-folded, with none of the quirks above. Every\n");
    printf("  disagreement is a quirk that has to be in the assembly; they are counted by SHAPE.\n");
    {
        static const char AL[5] = { 'a', 'b', '*', '?', '.' };
        static const char SL[3] = { 'a', 'b', '.' };
        char p[8], s[8];
        long pairs = 0, diff = 0;
        long d_trailq = 0, d_dot = 0, d_other = 0;
        int shown = 0;
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
                        /* textbook greedy matcher */
                        int si = 0, pi = 0, star = -1, ss = 0;
                        while (si < ls) {
                            if (pi < lp && (p[pi] == '?' || p[pi] == s[si])) { ++si; ++pi; }
                            else if (pi < lp && p[pi] == '*') { star = pi++; ss = si; }
                            else if (star >= 0) { pi = star + 1; si = ++ss; }
                            else break;
                        }
                        int ok;
                        if (si < ls) ok = 0;
                        else { while (pi < lp && p[pi] == '*') ++pi; ok = (pi == lp); }
                        int live = m(s, p);
                        if (live != ok) {
                            ++diff;
                            /* classify: a spare trailing '?', a '.' in the pattern, or something else */
                            int tq = (lp && p[lp-1] == '?');
                            int hasdot = 0;
                            for (int i = 0; i < lp; ++i) if (p[i] == '.') hasdot = 1;
                            if (tq && !hasdot) ++d_trailq;
                            else if (hasdot) ++d_dot;
                            else {
                                ++d_other;
                                if (shown < 15) {
                                    printf("    UNCLASSIFIED: subject \"%s\" pattern \"%s\" -> live %d, "
                                           "textbook %d\n", s, p, live, ok);
                                    ++shown;
                                }
                            }
                        }
                        ++pairs;
                    }
                }
            }
        }
        printf("\n    %ld pairs, %ld disagreements with the textbook matcher\n", pairs, diff);
        printf("    %ld involve a spare trailing '?', %ld involve a '.' in the pattern, "
               "%ld are neither\n", d_trailq, d_dot, d_other);
        printf("    => %s\n", d_other ? "there is a THIRD quirk still unaccounted for"
                                      : "the two quirks account for every disagreement");
    }
    return 0;
}
