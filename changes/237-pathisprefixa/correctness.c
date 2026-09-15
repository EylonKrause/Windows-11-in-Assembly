/* changes/237-pathisprefixa/correctness.c
   Gate 1: wia_pathisprefixa must be indistinguishable from shlwapi!PathIsPrefixA.
   Three-way: our assembly vs the independent scalar oracle vs the LIVE export on this PC.

   The whole contract is a BOOL, so the corpus is chosen for BRANCH coverage. Four things make that
   harder than it sounds, and each one is a separate sweep below.

     1. THE COMPARISON FOLDS 61 CLASSES, one of which -- 0x5E == 0x88 -- is not a case pair. An
        implementation that reached for a case-mapping API would be wrong on exactly that one, so the
        alphabet carries it, and every byte value is also swept against its own fold partner.

     2. THE ANSWER IS CUT AT A COMPONENT BOUNDARY, by two POSITIONAL rules (last separator at index 2,
        and index 1 behind a leading doubled separator). Those live at short lengths and in
        arrangements a realistic path corpus never contains, so {a, backslash, colon} is enumerated
        exhaustively on BOTH arguments.

     3. TWO ANOMALIES FALL OUT OF CHANGE 236'S REPORTED-COUNT DEFECT, and they are asserted DIRECTLY
        as well as covered by the enumeration, because they are the cases most likely to be
        "corrected" by a well-meaning implementation:
            a TWO-CHARACTER path is NOT a prefix of itself, at that length only;
            a path of length 3 ending in a separator IS a prefix of its own first two characters.

     4. LENGTH IS A DIMENSION. Change 236's model survived 3.65 million pairs and was still wrong,
        because every sweep ran to 250 characters and a rule started at 260. The long sweep here
        crosses that threshold in both directions.

   And the page-edge scalar step needs a guard page: impl.asm folds a single byte through the same
   instruction sequence at 128-bit width within 32 bytes of either page end, and nothing in an
   ordinary corpus reaches it. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_pathisprefixa(const char*, const char*);
int ref_pathisprefixa(const char*, const char*);
typedef BOOL (WINAPI *FN)(LPCSTR, LPCSTR);
static FN sys;

static long fails = 0;
static int  shown = 0;

static unsigned long sd = 0x5A17u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static void chk(const char* a, const char* b, const char* what)
{
    int r0 = !!wia_pathisprefixa(a, b);
    int r1 = !!ref_pathisprefixa(a, b);
    int r2 = !!sys(a, b);
    if (r0 != r1 || r0 != r2) {
        ++fails;
        if (shown < 20) {
            printf("FAIL %s: \"%s\" \"%s\" -> ours %d oracle %d live %d\n",
                   what, a ? a : "(NULL)", b ? b : "(NULL)", r0, r1, r2);
            ++shown;
        }
    }
}

#define ASSERT3(expr, want, msg) do {                                                  \
    if (!!(expr) != (want)) { printf("FAIL: %s\n", (msg)); ++fails; }                   \
} while (0)

static unsigned char FOLDP[256];
static void build_foldp(void){
    for (int v = 0; v < 256; ++v) FOLDP[v] = (unsigned char)v;
    for (int v = 0x61; v <= 0x7A; ++v) FOLDP[v] = (unsigned char)(v - 0x20);
    for (int v = 0xE0; v <= 0xF6; ++v) FOLDP[v] = (unsigned char)(v - 0x20);
    for (int v = 0xF8; v <= 0xFE; ++v) FOLDP[v] = (unsigned char)(v - 0x20);
    FOLDP[0x88] = 0x5E; FOLDP[0x9A] = 0x8A; FOLDP[0x9C] = 0x8C;
    FOLDP[0x9E] = 0x8E; FOLDP[0xFF] = 0x9F;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathIsPrefixA");
    if (!sys) { printf("CORRECTNESS: cannot resolve shlwapi!PathIsPrefixA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());
    build_foldp();

    /* ---- THE TWO ANOMALIES, asserted directly on all three ---------------------------------- */
    {
        /* a two-character path is not a prefix of itself -- at that length and no other */
        ASSERT3(wia_pathisprefixa("aa", "aa"), 0, "ours: a 2-char path is NOT a prefix of itself");
        ASSERT3(ref_pathisprefixa("aa", "aa"), 0, "oracle: a 2-char path is NOT a prefix of itself");
        ASSERT3(sys("aa", "aa"),               0, "live: a 2-char path is NOT a prefix of itself");
        char s[32];
        for (int n = 0; n <= 10; ++n) {
            for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
            s[n] = 0;
            chk(s, s, "self, every length");
            int want = (n != 2);
            ASSERT3(wia_pathisprefixa(s, s), want, "ours: the self-prefix hole is at length 2 only");
        }
        /* a longer path IS a prefix of a shorter one, for exactly one shape */
        ASSERT3(wia_pathisprefixa("xy\\", "xy"), 1, "ours: \"xy\\\" IS a prefix of \"xy\"");
        ASSERT3(ref_pathisprefixa("xy\\", "xy"), 1, "oracle: \"xy\\\" IS a prefix of \"xy\"");
        ASSERT3(sys("xy\\", "xy"),               1, "live: \"xy\\\" IS a prefix of \"xy\"");
        ASSERT3(wia_pathisprefixa("abc\\", "abc"), 0, "ours: but not at length 4");
    }

    /* ---- the probe-derived shapes ------------------------------------------------------------ */
    {
        static const char* V[][2] = {
            { "C:\\dir",       "C:\\dir\\file"   },
            { "C:\\dir\\file", "C:\\dir"         },
            { "C:\\dir",       "C:\\dir"         },
            { "C:\\dir",       "C:\\dirfile"     },
            { "C:\\",          "C:\\dir"         },
            { "C:",            "C:\\dir"         },
            { "",              "C:\\dir"         },
            { "C:\\dir",       ""                },
            { "",              ""                },
            { "\\",            "\\dir"           },
            { "\\",            "\\\\"            },
            { "\\\\srv\\shr",  "\\\\srv\\shr\\a" },
            { "\\\\srv\\shr",  "\\\\srv\\oth"    },
            { "aa",            "aa\\b"           },
            { "a\\",           "a\\b"            },
            { "C:\\DIR",       "c:\\dir\\file"   },
            { "x\\^",          "x\\\x88\\z"      },
            { "x\\\x88",       "x\\^\\z"         },
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i) chk(V[i][0], V[i][1], "probe case");
    }

    /* ---- NULL -------------------------------------------------------------------------------- */
    chk(NULL, "C:\\a", "NULL first");
    chk("C:\\a", NULL, "NULL second");
    chk(NULL, NULL,    "both NULL");

    /* ---- exhaustive over a FOLD-HEAVY alphabet ----------------------------------------------- */
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
                        chk(a, b, "exhaustive fold-heavy");
                        ++pairs;
                    }
                }
            }
        }
        printf("  exhaustive {a,A,backslash,:,0x5E,0x88} to length 4 both sides: %ld pairs\n", pairs);
    }

    /* ---- exhaustive over a PATH-SHAPED alphabet, deeper -------------------------------------- */
    {
        static const char AL[4] = { 'a', 'b', '\\', ':' };
        char a[16], b[16];
        long pairs = 0;
        for (int la = 0; la <= 5; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 4;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 4]; v /= 4; }
                a[la] = 0;
                for (int lb = 0; lb <= 5; ++lb) {
                    long cb = 1; for (int i = 0; i < lb; ++i) cb *= 4;
                    for (long kb = 0; kb < cb; ++kb) {
                        long w = kb;
                        for (int i = 0; i < lb; ++i) { b[i] = AL[w % 4]; w /= 4; }
                        b[lb] = 0;
                        chk(a, b, "exhaustive path-shaped");
                        ++pairs;
                    }
                }
            }
        }
        printf("  exhaustive {a,b,backslash,:} to length 5 both sides: %ld pairs\n", pairs);
    }

    /* ---- every byte value, and every ordered byte PAIR, inside a component -------------------- */
    {
        long cases = 0;
        for (int v = 1; v < 256; ++v) {
            for (int w = 1; w < 256; ++w) {
                char p[16], q[16];
                p[0]='x'; p[1]='\\'; p[2]=(char)v; p[3]=0;
                q[0]='x'; q[1]='\\'; q[2]=(char)w; q[3]='\\'; q[4]='z'; q[5]=0;
                chk(p, q, "byte pair inside a component"); ++cases;
            }
            /* and deep inside a longer path, past the first 32-byte block */
            char p[80], q[80];
            for (int i = 0; i < 70; ++i) {
                p[i] = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
                q[i] = p[i];
            }
            p[47] = (char)v; q[47] = (char)FOLDP[v];
            p[55] = 0;  /* p is a prefix ending mid-component */
            q[70] = 0;
            chk(p, q, "byte value at offset 47"); ++cases;
            p[55] = '\\'; p[56] = 0;
            chk(p, q, "byte value at offset 47, component-aligned"); ++cases;
        }
        printf("  all 255x255 ordered byte pairs inside a component, plus every byte value at\n"
               "  offset 47 against its fold partner: %ld cases\n", cases);
    }

    /* ---- 32 alignments x lengths, with the prefix cut at every position ---------------------- */
    {
        static char pa[1024], pb[1024];
        long cases = 0;
        for (int oa = 0; oa < 32; oa += 3) {
            for (int ob = 0; ob < 32; ob += 5) {
                for (int n = 1; n <= 70; ++n) {
                    char* a = pa + oa;
                    char* b = pb + ob;
                    for (int i = 0; i < n; ++i) {
                        b[i] = (i % 7 == 6) ? '\\' : (char)('a' + i % 23);
                        a[i] = b[i];
                    }
                    b[n] = 0;
                    for (int cut = 0; cut <= n; ++cut) {
                        a[cut] = 0;
                        chk(a, b, "aligned, a cut at every position"); ++cases;
                        chk(b, a, "aligned, reversed");               ++cases;
                        a[cut] = b[cut];
                    }
                    /* and a case-flipped copy, so the fold runs on every block */
                    for (int i = 0; i < n; ++i)
                        if (a[i] >= 'a' && a[i] <= 'z') a[i] = (char)(a[i] - 0x20);
                    a[n] = 0;
                    chk(a, b, "aligned, differing case"); ++cases;
                }
            }
        }
        printf("  alignments x lengths 1..70 with the prefix cut at EVERY position, both\n"
               "  directions, plus a case-flipped copy: %ld cases\n", cases);
    }

    /* ---- LENGTH AS A DIMENSION: across change 236's MAX_PATH threshold ----------------------- */
    {
        static char a[1200], b[1200];
        long cases = 0;
        for (int n = 240; n <= 620; ++n) {
            for (int i = 0; i < n; ++i) {
                b[i] = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
                a[i] = b[i];
            }
            b[n] = 0;
            /* a as a whole-component prefix, at several cuts either side of 260 */
            static const int CUTS[] = { 247, 255, 256, 258, 259, 260, 261, 263, 271, 279, -1 };
            for (int ci = 0; CUTS[ci] >= 0; ++ci) {
                int cut = CUTS[ci];
                if (cut >= n) continue;
                char save = a[cut]; a[cut] = 0;
                chk(a, b, "across MAX_PATH"); ++cases;
                chk(b, a, "across MAX_PATH, reversed"); ++cases;
                a[cut] = save;
            }
            a[n] = 0;
            chk(a, b, "identical, across MAX_PATH"); ++cases;
        }
        printf("  lengths 240..620 with cuts either side of 260, both directions: %ld cases\n",
               cases);
    }

    /* ---- fuzz, with FORCED common prefixes --------------------------------------------------- */
    {
        static const char AL[10] = { 'a','b','A','B','\\',':','/', 0x5E, (char)0x88, (char)0xE0 };
        static char a[256], b[256];
        for (int t = 0; t < 400000; ++t) {
            int nb = rnd() % 80;
            int na = rnd() % (nb + 1);
            for (int i = 0; i < nb; ++i) b[i] = AL[rnd() % 10];
            for (int i = 0; i < na; ++i) a[i] = b[i];
            /* sometimes perturb one character of a, sometimes case-flip it */
            if (na && (rnd() & 3) == 0) a[rnd() % na] = AL[rnd() % 10];
            if (na && (rnd() & 3) == 1)
                for (int i = 0; i < na; ++i)
                    if (a[i] >= 'a' && a[i] <= 'z') a[i] = (char)(a[i] - 0x20);
            a[na] = 0; b[nb] = 0;
            chk(a, b, "fuzz");
            chk(b, a, "fuzz reversed");
        }
        printf("  400000 fuzz pairs with FORCED common prefixes, both directions\n");
    }

    /* ---- THE SCALAR PATH: both strings at a PAGE_NOACCESS page -------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bb = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bb+pg, pg, PAGE_NOACCESS, &old);
        static const unsigned char FA[12] = { 'a','A',0x5E,0x88,0x9A,0x8A,0x9F,0xFF,0xE0,0xC0,
                                              0xF8,0xD8 };
        long cases = 0;
        for (int tail = 2; tail <= 200; ++tail) {
            for (int shape = 0; shape < 4; ++shape) {
                char* a = (ba+pg) - tail;
                char* b = (bb+pg) - tail;
                for (int i = 0; i < tail-1; ++i) {
                    b[i] = (char)FA[(i + shape) % 12];
                    if (shape == 1 && i % 6 == 5) b[i] = '\\';
                    if (shape == 2 && i == 2)     b[i] = '\\';
                    if (shape == 3 && i < 2)      b[i] = '\\';
                    a[i] = b[i];
                }
                a[tail-1] = 0; b[tail-1] = 0;
                chk(a, b, "guard, identical"); ++cases;
                chk(b, a, "guard, reversed");  ++cases;
                /* fold-equal but not byte-equal, so the scalar fold must decide */
                for (int i = 0; i < tail-1; ++i) {
                    unsigned char c = (unsigned char)b[i];
                    if (c >= 'A' && c <= 'Z') a[i] = (char)(c + 0x20);
                    else if (c == 0x5E)       a[i] = (char)0x88;
                    else if (c == 0x8A)       a[i] = (char)0x9A;
                    else if (c == 0x9F)       a[i] = (char)0xFF;
                    else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) a[i] = (char)(c + 0x20);
                }
                chk(a, b, "guard, fold-equal"); ++cases;
                /* and a divergence at the very last readable character */
                a[tail-2] = (char)(b[tail-2] == 'q' ? 'r' : 'q');
                chk(a, b, "guard, differs at the last character"); ++cases;
            }
        }
        printf("  guard-page sweep, BOTH strings at a NOACCESS page, four shapes: %ld cases\n",
               cases);
        VirtualFree(ba,0,MEM_RELEASE); VirtualFree(bb,0,MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%ld)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathIsPrefixA vs live shlwapi + an independent oracle. THE TWO\n"
           "ANOMALIES THAT FALL OUT OF CHANGE 236'S REPORTED-COUNT DEFECT ARE ASSERTED DIRECTLY,\n"
           "because they are what a well-meaning implementation would 'correct': a TWO-CHARACTER\n"
           "path is NOT a prefix of itself -- at that length and no other, lengths 0, 1 and 3..10\n"
           "all checked -- and a path of length 3 ending in a separator IS a prefix of its own\n"
           "first two characters, while length 4 is not. Corpus: the probe-derived shapes; NULL in\n"
           "both positions; exhaustive {a,A,backslash,colon,0x5E,0x88} to length 4 on BOTH arguments,\n"
           "an alphabet carrying a case pair AND the 0x5E/0x88 conflation that no case-mapping API\n"
           "reproduces; exhaustive {a,b,backslash,colon} to length 5 on both, which reaches the two\n"
           "POSITIONAL cut rules; all 255x255 ordered byte pairs inside a component plus every byte\n"
           "value at offset 47 against its fold partner, past the first 32-byte block; alignments x\n"
           "lengths 1..70 with the prefix cut at EVERY position in BOTH directions; lengths 240..620\n"
           "with cuts either side of 260, because LENGTH IS A DIMENSION and change 236's model\n"
           "survived 3.65 million pairs while being wrong about it; 400000 fuzz pairs with FORCED\n"
           "common prefixes; and a guard-page sweep with BOTH strings ending at a NOACCESS page in\n"
           "four shapes, identical / fold-equal / divergent, which is the only thing that reaches\n"
           "the one-byte scalar step and its 128-bit fold)\n");
    return 0;
}
