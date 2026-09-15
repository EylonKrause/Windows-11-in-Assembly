/* changes/236-pathcommonprefixa/correctness.c
   Gate 1: wia_pathcommonprefixa must be indistinguishable from shlwapi!PathCommonPrefixA.
   Three-way: our assembly vs the independent scalar oracle vs the LIVE export on this PC.

   EVERY COMPARISON INCLUDES THE WHOLE OUTPUT WINDOW, POISON AND ALL. The return alone is not the
   contract here, and three separate measured facts say so:

     * a common prefix of exactly 2 is REPORTED as 3 while only two characters are written, so a
       harness that checked only the return would pass an implementation that wrote the third;
     * NULL writes NOTHING AT ALL, not even a terminator, while a valid pair with no common prefix
       DOES write one -- two cases that look identical unless the untouched bytes are inspected;
     * the copy is bounded by the string, so an implementation that copied n bytes unconditionally
       would read past a caller's terminator and still return the right number.

   THE CORPUS IS CHOSEN FOR THE TWO HALVES OF THE FUNCTION.

     The COMPARISON half needs bytes that fold. The alphabet therefore carries a case pair AND the
     0x5E/0x88 conflation, and every byte value is swept against its own fold partner -- an
     implementation that folded only ASCII, or that reached for a case-mapping API, fails on 0x88
     and on the thirty CP1252 accented pairs.

     The CUT half needs separators in every arrangement, including the two positional rules -- last
     separator at index 2, and index 1 behind a leading doubled separator -- so the path-shaped
     alphabet {a, backslash, colon} is enumerated exhaustively to length 6 on BOTH arguments.

   AND THE SCALAR PATH NEEDS A PAGE BOUNDARY. impl.asm steps one byte at a time within 32 bytes of
   either page end, folding that byte with the same instruction sequence at 128-bit width. Nothing
   in an ordinary corpus reaches that code, so there is a dedicated sweep that walks BOTH strings up
   to a PAGE_NOACCESS page across the full byte alphabet. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_pathcommonprefixa(const char*, const char*, char*);
int ref_pathcommonprefixa(const char*, const char*, char*);
typedef int (WINAPI *FN)(LPCSTR, LPCSTR, LPSTR);
static FN sys;

#define POISON 0xCD
#define WIN 400
static char oo[WIN], oref[WIN], osys[WIN];

static long fails = 0;
static int  shown = 0;

static unsigned long sd = 0x9E37u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* Compare all three, return included and the whole poison window. */
static void chk(const char* a, const char* b, const char* what)
{
    memset(oo, POISON, WIN); memset(oref, POISON, WIN); memset(osys, POISON, WIN);
    int r0 = wia_pathcommonprefixa(a, b, oo);
    int r1 = ref_pathcommonprefixa(a, b, oref);
    int r2 = sys(a, b, osys);
    int ok = (r0 == r1) && (r0 == r2)
          && memcmp(oo, oref, WIN) == 0 && memcmp(oo, osys, WIN) == 0;
    if (!ok) {
        ++fails;
        if (shown < 20) {
            printf("FAIL %s: \"%s\" \"%s\" -> ours %d \"%s\" | oracle %d \"%s\" | live %d \"%s\"\n",
                   what, a ? a : "(NULL)", b ? b : "(NULL)",
                   r0, oo, r1, oref, r2, osys);
            ++shown;
        }
    }
}

/* the out == NULL form, which must not fault and must return the same number */
static void chk_nobuf(const char* a, const char* b, const char* what)
{
    int r0 = wia_pathcommonprefixa(a, b, NULL);
    int r1 = ref_pathcommonprefixa(a, b, NULL);
    int r2 = sys(a, b, NULL);
    if (r0 != r1 || r0 != r2) {
        ++fails;
        if (shown < 20) {
            printf("FAIL %s (no buffer): \"%s\" \"%s\" -> ours %d oracle %d live %d\n",
                   what, a ? a : "(NULL)", b ? b : "(NULL)", r0, r1, r2);
            ++shown;
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathCommonPrefixA");
    if (!sys) { printf("CORRECTNESS: cannot resolve shlwapi!PathCommonPrefixA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    /* ---- the shapes the probes singled out ---------------------------------------------------- */
    {
        static const char* V[][2] = {
            { "C:\\aaa\\bbb", "C:\\aaa\\ccc" },  /* separator dropped */
            { "C:\\aaabbb",   "C:\\aaaccc"   },  /* separator KEPT at index 2 */
            { "C:\\aaa\\bbb", "C:\\aaa"      },  /* a whole component: uncut */
            { "C:\\aaa\\bbb", "C:\\aaa\\"    },
            { "C:\\aaa",      "C:\\aaa"      },
            { "C:\\aaa",      "D:\\aaa"      },
            { "aaa\\bbb",     "aaa\\ccc"     },
            { "\\\\srv\\shr\\a", "\\\\srv\\shr\\b" },
            { "\\\\srv\\shr",    "\\\\srv\\oth"    },
            { "abc",          "abd"          },
            { "",             ""             },
            { "C:\\",         "C:\\"         },
            { "aa",           "aa"           },  /* the length-2 defect: returns 3, writes 2 */
            { "aa\\",         "aa"           },  /* ... and here it returns 3 and writes 3 */
            { "a",            "a\\"          },  /* uncut */
            { "\\",           "\\\\"         },  /* ... but not for a lone separator */
            { "\\",           "\\"           },
            { "C:\\AAA\\b",   "c:\\aaa\\c"   },  /* folded, and the FIRST path is the one copied */
            { "c:\\aaa\\b",   "C:\\AAA\\c"   },
            { "x\\^",         "x\\\x88"      },  /* the 0x5E/0x88 conflation */
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i) { chk(V[i][0], V[i][1], "probe case");
                                       chk_nobuf(V[i][0], V[i][1], "probe case"); }
    }

    /* ---- NULL: writes NOTHING, unlike a valid pair with no common prefix --------------------- */
    chk(NULL, "C:\\a", "NULL first");
    chk("C:\\a", NULL, "NULL second");
    chk(NULL, NULL,    "both NULL");
    chk("abc", "xyz",  "no common prefix (DOES write a terminator)");
    chk_nobuf(NULL, "C:\\a", "NULL first");
    chk_nobuf(NULL, NULL,    "both NULL");

    /* ---- exhaustive over a FOLD-HEAVY alphabet ------------------------------------------------ */
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
        printf("  exhaustive {a,A,backslash,:,0x5E,0x88} to length 4: %ld pairs\n", pairs);
    }

    /* ---- exhaustive over a PATH-SHAPED alphabet, deeper -------------------------------------- */
    {
        static const char AL[3] = { 'a', '\\', ':' };
        char a[16], b[16];
        long pairs = 0;
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
                        chk(a, b, "exhaustive path-shaped");
                        ++pairs;
                    }
                }
            }
        }
        printf("  exhaustive {a,backslash,:} to length 6: %ld pairs\n", pairs);
    }

    /* ---- every byte value, against itself and against its fold partner ----------------------- */
    {
        long cases = 0;
        for (int v = 1; v < 256; ++v) {
            char a[64], b[64];
            for (int pos = 0; pos < 10; ++pos) {
                strcpy(a, "C:\\dir\\sub\\file.txt");
                strcpy(b, "C:\\dir\\sub\\file.txt");
                a[3 + pos] = (char)v;
                chk(a, b, "byte value, one side"); ++cases;
                b[3 + pos] = (char)v;
                chk(a, b, "byte value, both sides"); ++cases;
                /* and against every OTHER byte value at one position, for one position only */
            }
            for (int w = 1; w < 256; ++w) {
                char c[16], d[16];
                c[0]='x'; c[1]='\\'; c[2]=(char)v; c[3]='\\'; c[4]='z'; c[5]=0;
                d[0]='x'; d[1]='\\'; d[2]=(char)w; d[3]='\\'; d[4]='z'; d[5]=0;
                chk(c, d, "byte value vs byte value"); ++cases;
            }
        }
        printf("  all 255 byte values at 10 positions, and all 255x255 ordered pairs "
               "inside a component: %ld cases\n", cases);
    }

    /* ---- 32 alignments x lengths, divergence and case flip at every position ------------------ */
    {
        static char pa[1024], pb[1024];
        long cases = 0;
        for (int oa = 0; oa < 32; oa += 3) {
            for (int ob = 0; ob < 32; ob += 5) {
                for (int n = 1; n <= 70; ++n) {
                    char* a = pa + oa;
                    char* b = pb + ob;
                    for (int i = 0; i < n; ++i) {
                        a[i] = (i % 7 == 6) ? '\\' : (char)('a' + i % 23);
                        b[i] = a[i];
                    }
                    a[n] = 0; b[n] = 0;
                    chk(a, b, "aligned, identical"); ++cases;
                    for (int pos = 0; pos < n; ++pos) {
                        char save = b[pos];
                        b[pos] = 'Z';                     chk(a, b, "aligned, differs"); ++cases;
                        if (save >= 'a' && save <= 'z') {
                            b[pos] = (char)(save - 0x20); chk(a, b, "aligned, case flip"); ++cases;
                        }
                        b[pos] = save;
                    }
                }
            }
        }
        printf("  alignments x lengths 1..70 with a divergence and a case flip at every "
               "position: %ld cases\n", cases);
    }

    /* ---- long paths, and one a strict prefix of the other at every cut ------------------------ */
    {
        static char a[1200], b[1200];
        long cases = 0;
        for (int shape = 0; shape < 3; ++shape) {
            for (int n = 8; n <= 600; n += 29) {
                for (int i = 0; i < n; ++i) {
                    if (shape == 0)      a[i] = (i % 7 == 6) ? '\\' : (char)('a' + i % 23);
                    else if (shape == 1) a[i] = (char)('a' + i % 23);        /* no separator at all */
                    else                 a[i] = (i < 3) ? "C:\\"[i]
                                              : ((i % 5 == 4) ? '\\' : (char)('a' + i % 23));
                    b[i] = a[i];
                }
                a[n] = 0; b[n] = 0;
                chk(a, b, "long, identical"); ++cases;
                for (int cut = 0; cut <= n; cut += 3) {
                    char save = b[cut]; b[cut] = 0;
                    chk(a, b, "long, b a strict prefix"); ++cases;
                    chk(b, a, "long, a a strict prefix"); ++cases;
                    b[cut] = save;
                }
                for (int pos = 0; pos < n; pos += 7) {
                    char save = b[pos];
                    b[pos] = 'Q'; chk(a, b, "long, differs"); ++cases;
                    b[pos] = save;
                }
            }
        }
        printf("  long paths to 600 in three shapes, prefix cuts and divergences: %ld cases\n",
               cases);
    }

    /* ---- fuzz -------------------------------------------------------------------------------- */
    {
        static const char AL[10] = { 'a','b','A','B','\\',':','/', 0x5E, (char)0x88, (char)0xE0 };
        static char a[256], b[256];
        for (int t = 0; t < 300000; ++t) {
            int na = rnd() % 60, nb = rnd() % 60;
            int shared = rnd() % (na < nb ? (na + 1) : (nb + 1));
            for (int i = 0; i < na; ++i) a[i] = AL[rnd() % 10];
            for (int i = 0; i < nb; ++i) b[i] = AL[rnd() % 10];
            for (int i = 0; i < shared; ++i) b[i] = a[i];   /* force real common prefixes */
            a[na] = 0; b[nb] = 0;
            chk(a, b, "fuzz");
        }
        printf("  300000 fuzz pairs over a separator- and fold-heavy alphabet\n");
    }

    /* ---- THE SCALAR PATH: both strings walked up to a PAGE_NOACCESS page --------------------- */
    /* This is the only sweep that reaches impl.asm's one-byte step and its 128-bit fold. The
       alphabet rotates through the values that fold, so the scalar fold is exercised on the ranges
       AND on all five singletons. */
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
                    a[i] = (char)FA[(i + shape) % 12];
                    if (shape == 1 && i % 6 == 5) a[i] = '\\';
                    if (shape == 2 && i == 2)     a[i] = '\\';
                    if (shape == 3 && i < 2)      a[i] = '\\';
                    b[i] = a[i];
                }
                a[tail-1] = 0; b[tail-1] = 0;
                /* identical, both at a guard */
                chk(a, b, "guard, identical"); ++cases;
                chk_nobuf(a, b, "guard, identical"); ++cases;
                /* fold-equal but not byte-equal, so the scalar fold must decide */
                for (int i = 0; i < tail-1; ++i) {
                    unsigned char c = (unsigned char)a[i];
                    if (c >= 'A' && c <= 'Z') b[i] = (char)(c + 0x20);
                    else if (c == 0x5E)       b[i] = (char)0x88;
                    else if (c == 0x8A)       b[i] = (char)0x9A;
                    else if (c == 0x9F)       b[i] = (char)0xFF;
                    else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) b[i] = (char)(c + 0x20);
                }
                chk(a, b, "guard, fold-equal"); ++cases;
                /* and a genuine divergence at the very last readable character */
                b[tail-2] = (char)(a[tail-2] == 'q' ? 'r' : 'q');
                chk(a, b, "guard, differs at the last character"); ++cases;
            }
        }
        printf("  guard-page sweep, BOTH strings at a NOACCESS page, four shapes, "
               "identical / fold-equal / divergent: %ld cases\n", cases);
        VirtualFree(ba,0,MEM_RELEASE); VirtualFree(bb,0,MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%ld)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathCommonPrefixA vs live shlwapi + an independent oracle, comparing\n"
           "the return AND a 400-byte POISON WINDOW on every case -- because the contract needs it:\n"
           "a common prefix of exactly 2 is REPORTED as 3 while only two characters are written,\n"
           "NULL writes nothing at all while a valid pair with no common prefix does write a\n"
           "terminator, and the copy is bounded by the string. Corpus: the probe-derived shapes;\n"
           "NULL in both positions; exhaustive {a,A,backslash,colon,0x5E,0x88} to length 4 on BOTH\n"
           "arguments, an alphabet carrying a case pair AND the 0x5E/0x88 conflation that no\n"
           "case-mapping API reproduces; exhaustive {a,backslash,colon} to length 6 on both, which\n"
           "reaches the two POSITIONAL cut rules; all 255 byte values at 10 positions plus all\n"
           "255x255 ordered pairs inside a component; alignments x lengths 1..70 with a divergence\n"
           "and a case flip at EVERY position; long paths to 600 in three shapes with prefix cuts;\n"
           "300000 fuzz pairs with forced common prefixes; and a guard-page sweep with BOTH strings\n"
           "ending at a NOACCESS page in four shapes, which is the only thing that reaches the\n"
           "one-byte scalar step and its 128-bit fold)\n");
    return 0;
}
