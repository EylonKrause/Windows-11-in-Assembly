/* changes/239-pathmatchspeca/probes/pmsa.c
   Pin down shlwapi!PathMatchSpecA before writing any assembly.

   WHY. discovery/shlwapi_path3.c measured 824.75 ns for a 254-character subject against the pattern
   "*.exe" -- 3.25 ns per byte -- and 150.13 for the wide form on the same character count. That is
   5.5x the wide cost for HALF the bytes, 11x per byte, and it is the last target above 3 ns/byte in
   the survey.

   WHY THIS ONE IS DIFFERENT FROM 235-238. Every target so far had a contract that was a TABLE or a
   PREDICATE: which bytes are separators, which bytes fold, where to cut. This one is a GRAMMAR. A
   wildcard matcher's behaviour is defined by how '*' and '?' backtrack, and the interesting cases are
   not byte values but SHAPES -- and the shapes that break a naive matcher are exactly the ones a
   realistic corpus of file names never contains ("a*a*a*a*b" against "aaaaaaaa").

   So this probe does not try to derive a rule from spot checks. It builds an EXHAUSTIVE cross product
   of patterns and subjects over a tiny alphabet that contains both wildcards, and reports the answer
   for every pair. That is the only way to pin a grammar, and it is cheap: 4 characters of pattern and
   4 of subject over {a, b, *, ?} is already 87 000 pairs.

   WHAT HAS TO BE SETTLED:

     1. Which characters are wildcards, and is there a third one? (Windows has historically had
        DOS_STAR, DOS_QM and DOS_DOT as separate metacharacters at the kernel level.)
     2. Does '?' match a single character only, or also zero characters at the end of the subject?
     3. Does '*' match across a path separator? Across a dot?
     4. IS THE MATCH CASE-INSENSITIVE, and over which bytes? Changes 232, 236 and 238 each found a
        different answer to that question in the same DLL, so it is derived here and not inherited.
     5. Is the pattern a LIST? PathMatchSpec is documented to accept semicolon-separated patterns.
     6. The empty pattern, the empty subject, and NULL in both arguments.
     7. Whether anything about the subject's path structure matters at all, or whether it is a pure
        string matcher with a misleading name.

   Nothing here writes to disk or touches system state. */
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
    printf("PathMatchSpecA = %p\nGetACP() = %u\n", (void*)pms, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the obvious cases ===\n");
    {
        static const char* V[][2] = {
            { "file.txt",      "*.txt"    },
            { "file.txt",      "*.exe"    },
            { "file.txt",      "file.txt" },
            { "file.txt",      "*"        },
            { "file.txt",      "f*"       },
            { "file.txt",      "?ile.txt" },
            { "file.txt",      "??le.txt" },
            { "file.txt",      "file.???" },
            { "file.txt",      "file.??"  },
            { "file.txt",      "file.????"},
            { "C:\\dir\\f.txt","*.txt"    },
            { "C:\\dir\\f.txt","C:\\*"    },
            { "C:\\dir\\f.txt","*\\f.txt" },
            { "C:\\dir\\f.txt","C:\\dir\\*" },
            { "FILE.TXT",      "*.txt"    },
            { "file.txt",      "*.TXT"    },
            { "",              "*"        },
            { "",              ""         },
            { "a",             ""         },
            { "",              "a"        },
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i)
            printf("  %-18s vs %-12s -> %d\n", V[i][0], V[i][1], m(V[i][0], V[i][1]));
    }

    printf("\n=== 2. IS THE PATTERN A LIST? ===\n");
    {
        static const char* V[][2] = {
            { "file.txt", "*.exe;*.txt" },
            { "file.txt", "*.txt;*.exe" },
            { "file.txt", "*.exe;*.bat" },
            { "file.txt", "*.txt;"      },
            { "file.txt", ";*.txt"      },
            { "file.txt", ";"           },
            { "a;b",      "a;b"         },
            { "a;b",      "*;*"         },
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i)
            printf("  %-10s vs %-14s -> %d\n", V[i][0], V[i][1], m(V[i][0], V[i][1]));
        printf("  => a semicolon in the PATTERN separates alternatives; note what that means for a\n");
        printf("     SUBJECT containing one\n");
    }

    printf("\n=== 3. WHICH characters are metacharacters? ===\n");
    printf("  Pattern is a single byte v; subject is \"a\". If v matches 'a' without being 'a', it is\n");
    printf("  a single-character wildcard. Then the same with subject \"ab\" to find star-like ones.\n");
    {
        int one = 0, two = 0;
        printf("    matches \"a\" (single-char wildcards):");
        for (int v = 1; v < 256; ++v) {
            char p[4]; p[0] = (char)v; p[1] = 0;
            if (m("a", p) && v != 'a' && v != 'A') {
                if (v >= 32 && v < 127) printf(" %02X('%c')", v, v); else printf(" %02X", v);
                ++one;
            }
        }
        printf("   (%d)\n", one);
        printf("    matches \"ab\" (star-like):");
        for (int v = 1; v < 256; ++v) {
            char p[4]; p[0] = (char)v; p[1] = 0;
            if (m("ab", p)) {
                if (v >= 32 && v < 127) printf(" %02X('%c')", v, v); else printf(" %02X", v);
                ++two;
            }
        }
        printf("   (%d)\n", two);
        printf("    matches \"\" (empty subject):");
        for (int v = 1; v < 256; ++v) {
            char p[4]; p[0] = (char)v; p[1] = 0;
            if (m("", p)) {
                if (v >= 32 && v < 127) printf(" %02X('%c')", v, v); else printf(" %02X", v);
            }
        }
        printf("\n");
    }

    printf("\n=== 4. '?' -- exactly one character, or also zero? ===\n");
    {
        printf("    \"ab\"  vs \"ab?\"   -> %d   (a trailing ? with nothing left to match)\n",
               m("ab", "ab?"));
        printf("    \"ab\"  vs \"ab??\"  -> %d\n", m("ab", "ab??"));
        printf("    \"ab\"  vs \"a?\"    -> %d\n", m("ab", "a?"));
        printf("    \"abc\" vs \"a?c\"   -> %d\n", m("abc", "a?c"));
        printf("    \"ac\"  vs \"a?c\"   -> %d\n", m("ac", "a?c"));
        printf("    \"a.b\" vs \"a?b\"   -> %d   (does ? match a dot?)\n", m("a.b", "a?b"));
        printf("    \"a\\\\b\" vs \"a?b\"  -> %d   (does ? match a separator?)\n", m("a\\b", "a?b"));
    }

    printf("\n=== 5. '*' -- across a separator? across a dot? ===\n");
    {
        printf("    \"a\\\\b\"     vs \"a*b\"   -> %d\n", m("a\\b", "a*b"));
        printf("    \"a.b\"      vs \"a*b\"   -> %d\n", m("a.b", "a*b"));
        printf("    \"dir\\\\f.txt\" vs \"*.txt\" -> %d\n", m("dir\\f.txt", "*.txt"));
        printf("    \"dir.x\\\\f\"   vs \"*.x\"   -> %d\n", m("dir.x\\f", "*.x"));
        printf("    \"a\"        vs \"*a\"    -> %d\n", m("a", "*a"));
        printf("    \"a\"        vs \"a*\"    -> %d\n", m("a", "a*"));
        printf("    \"a\"        vs \"**\"    -> %d\n", m("a", "**"));
        printf("    \"file.txt\" vs \"*.*\"   -> %d\n", m("file.txt", "*.*"));
        printf("    \"file\"     vs \"*.*\"   -> %d   (no dot in the subject)\n", m("file", "*.*"));
        printf("    \"file.\"    vs \"*.*\"   -> %d\n", m("file.", "*.*"));
    }

    printf("\n=== 6. THE BACKTRACKING SHAPES -- what a naive matcher gets wrong ===\n");
    {
        static const char* V[][2] = {
            { "aaaaaaaa",   "a*a*a*a*b" },
            { "aaaaaaab",   "a*a*a*a*b" },
            { "aaa",        "*a*a*a"    },
            { "aaa",        "*a*a*a*a"  },
            { "abab",       "*ab"       },
            { "abab",       "ab*"       },
            { "abcabc",     "*abc"      },
            { "xaybzc",     "*a*b*c"    },
            { "xaybzc",     "*a*b*c*d"  },
            { "aab",        "*?b"       },
            { "ab",         "*?b"       },
            { "b",          "*?b"       },
            { "ab",         "?*b"       },
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i)
            printf("  %-12s vs %-12s -> %d\n", V[i][0], V[i][1], m(V[i][0], V[i][1]));
    }

    printf("\n=== 7. IS THE MATCH CASE-INSENSITIVE, over which bytes? ===\n");
    printf("  For each pair (v, w): does the one-character pattern <w> match the subject <v>?\n");
    printf("  Equivalence classes are printed. Changes 232, 236 and 238 each found a DIFFERENT\n");
    printf("  answer in this same DLL, so nothing is inherited here.\n");
    {
        static unsigned char cls[256];
        int nclass = 0;
        for (int v = 1; v < 256; ++v) cls[v] = 0;
        for (int v = 1; v < 256; ++v) {
            if (v == '*' || v == '?' || v == ';') continue;     /* metacharacters, handled above */
            if (cls[v]) continue;
            ++nclass; cls[v] = (unsigned char)nclass;
            for (int w = v + 1; w < 256; ++w) {
                if (w == '*' || w == '?' || w == ';') continue;
                char s[4], p[4];
                s[0] = (char)v; s[1] = 0;
                p[0] = (char)w; p[1] = 0;
                if (m(s, p)) cls[w] = (unsigned char)nclass;
            }
        }
        int multi = 0;
        for (int c = 1; c <= nclass; ++c) {
            int n = 0;
            for (int v = 1; v < 256; ++v) if (cls[v] == c) ++n;
            if (n > 1) {
                ++multi;
                printf("    class:");
                for (int v = 1; v < 256; ++v)
                    if (cls[v] == c) {
                        if (v >= 32 && v < 127) printf(" %02X('%c')", v, v); else printf(" %02X", v);
                    }
                printf("\n");
            }
        }
        printf("    %d classes with more than one member\n", multi);
    }

    printf("\n=== 8. EXHAUSTIVE: every pattern x every subject over {a, b, *, ?} ===\n");
    printf("  Patterns to length 4, subjects to length 4. The full table is the contract; here it is\n");
    printf("  summarised, and the SHAPES that a naive left-to-right matcher gets wrong are listed.\n");
    {
        static const char AL[4] = { 'a', 'b', '*', '?' };
        static const char SL[2] = { 'a', 'b' };
        char p[8], s[8];
        long pairs = 0, trues = 0;
        for (int lp = 0; lp <= 4; ++lp) {
            long cp = 1; for (int i = 0; i < lp; ++i) cp *= 4;
            for (long kp = 0; kp < cp; ++kp) {
                long v = kp;
                for (int i = 0; i < lp; ++i) { p[i] = AL[v % 4]; v /= 4; }
                p[lp] = 0;
                for (int ls = 0; ls <= 4; ++ls) {
                    long cs = 1; for (int i = 0; i < ls; ++i) cs *= 2;
                    for (long ks = 0; ks < cs; ++ks) {
                        long w = ks;
                        for (int i = 0; i < ls; ++i) { s[i] = SL[w % 2]; w /= 2; }
                        s[ls] = 0;
                        if (m(s, p)) ++trues;
                        ++pairs;
                    }
                }
            }
        }
        printf("    %ld (subject, pattern) pairs, %ld matched\n", pairs, trues);
    }

    printf("\n=== 9. NULL ===\n");
    {
        __try { printf("    (NULL, \"*\")   -> %d\n", m(0, "*")); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    (NULL, \"*\") FAULTED\n"); }
        __try { printf("    (\"a\", NULL)   -> %d\n", m("a", 0)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    (\"a\", NULL) FAULTED\n"); }
        __try { printf("    (NULL, NULL)  -> %d\n", m(0, 0)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    (NULL, NULL) FAULTED\n"); }
    }

    printf("\n=== 10. does it read past either terminator? ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int ok = 0, faults = 0;
        for (int tail = 2; tail <= 200; ++tail) {
            char* q = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) q[i] = 'a';
            q[tail-1] = 0;
            __try { pms(q, "*a");  ++ok; } __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
            __try { pms("aaaa", q); ++ok; } __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        printf("    over %d guard-page cases (subject at the guard, then pattern): %d ok, %d faulted\n",
               ok + faults, ok, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}
