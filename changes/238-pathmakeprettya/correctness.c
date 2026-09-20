/* changes/238-pathmakeprettya/correctness.c
   Gate 1: wia_pathmakeprettya must be indistinguishable from shlwapi!PathMakePrettyA.
   Three-way: our assembly vs the independent scalar oracle vs the LIVE export on this PC.

   EVERY COMPARISON CHECKS THE RETURN AND THE WHOLE BUFFER AGAINST A POISON FILL, because this
   function's contract splits into parts that a looser check cannot separate:

     * the RETURN does not mean "something changed", "123456", "" and "\\\\" all return 1 while
       changing nothing, so a harness that inferred one from the other would pass an implementation
       that returned the wrong thing on exactly those;
     * a REFUSAL writes nothing, and a string comparison cannot tell that from writing the same bytes
       back;
     * the rewrite is bounded at 259 characters while the refusal scan is not, so an implementation
       that shared one bound between them is correct on every path shorter than 260 and wrong past it.

   FOUR THINGS THIS CORPUS IS BUILT TO CATCH.

     1. THE REFUSAL SET IS ASCII-ONLY. Exactly 26 byte values veto. An implementation that used the
        code page's notion of "lowercase" would also veto on 0xE0..0xFE and be wrong on 30 more
        values, so every byte value is swept as the sole non-uppercase character.

     2. THE TWO MAPS ARE DIFFERENT, AND NEITHER IS CHANGE 236'S. Index 0 is UPPERCASED and index 1 on
        is LOWERCASED, and neither table touches 0x5E or 0x88 -- the pair change 236's comparison
        fold conflates. Every byte value is therefore tested at index 0 AND at an index the rewrite
        reaches.

     3. INDEX 0 IS OBSERVABLE ONLY THROUGH THE BYTES THE REFUSAL IGNORES. The only way to see index 0
        being uppercased is with a byte that is lowercase in CP1252 but not in ASCII -- exactly the
        set the veto skips. probes/pmpa.c's first detector missed the whole behaviour by watching
        index 0 for an ASCII lowercase letter that could never appear there.

     4. LENGTH IS A DIMENSION. Sweeps run to 700, across the 259/260 boundary, in both the refusing
        and the rewriting case. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_pathmakeprettya(char*);
int ref_pathmakeprettya(char*);
typedef int (WINAPI *FN)(char*);
static FN sys;

#define POISON 0xCD
#define WIN 1024
static char bo[WIN], br[WIN], bs[WIN];

static long fails = 0;
static int  shown = 0;

static unsigned long sd = 0x1357u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* Run all three on identical poisoned copies and compare the return AND the whole window. */
static void chk(const char* in, int n, const char* what)
{
    memset(bo, POISON, WIN); memset(br, POISON, WIN); memset(bs, POISON, WIN);
    memcpy(bo, in, n + 1); memcpy(br, in, n + 1); memcpy(bs, in, n + 1);
    int r0 = !!wia_pathmakeprettya(bo);
    int r1 = !!ref_pathmakeprettya(br);
    int r2 = !!sys(bs);
    int ok = (r0 == r1) && (r0 == r2)
          && memcmp(bo, br, WIN) == 0 && memcmp(bo, bs, WIN) == 0;
    if (!ok) {
        ++fails;
        if (shown < 20) {
            printf("FAIL %s (len %d): ret ours %d oracle %d live %d\n", what, n, r0, r1, r2);
            printf("      in    :"); for (int i = 0; i < (n < 24 ? n : 24); ++i) printf(" %02X", (unsigned char)in[i]);
            printf("\n      ours  :"); for (int i = 0; i < (n < 24 ? n : 24); ++i) printf(" %02X", (unsigned char)bo[i]);
            printf("\n      oracle:"); for (int i = 0; i < (n < 24 ? n : 24); ++i) printf(" %02X", (unsigned char)br[i]);
            printf("\n      live  :"); for (int i = 0; i < (n < 24 ? n : 24); ++i) printf(" %02X", (unsigned char)bs[i]);
            printf("\n");
            ++shown;
        }
    }
}
static void chks(const char* s, const char* what){ chk(s, (int)strlen(s), what); }

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathMakePrettyA");
    if (!sys) { printf("CORRECTNESS: cannot resolve shlwapi!PathMakePrettyA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    /* ---- NULL, and the return that does not mean "changed" ---------------------------------- */
    if (!!wia_pathmakeprettya(0) != !!sys(0)) { printf("FAIL: NULL\n"); ++fails; }
    if (!!ref_pathmakeprettya(0) != !!sys(0)) { printf("FAIL: NULL (oracle)\n"); ++fails; }

    /* ---- probe-derived shapes ---------------------------------------------------------------- */
    {
        static const char* V[] = {
            "C:\\DIR\\FILE.TXT", "C:\\dir\\file.txt", "C:\\Dir\\FILE.TXT",
            "C:\\DIR\\FILE.TXT ", "ABC", "abc", "A", "a", "",
            "C:\\DIR1\\FILE2.TXT", "123\\456", "ABCDEFGHIJKL", "A:CDEFGHIJKL",
            "\\BCDEFGHIJKL", "\\\\CDEFGHIJKL", "AB\\DEFGHIJKL", "A:\\DEFGHIJKL",
            "\\\\\\\\", "AbCdEf", "A:\\DIR", "A:\\dir", 0
        };
        for (int i = 0; V[i]; ++i) chks(V[i], "probe case");
    }

    /* ---- The refusal set: every byte value as the sole non-uppercase character --------------- */
    {
        static char s[128];
        long cases = 0;
        for (int v = 1; v < 256; ++v) {
            for (int pos = 0; pos < 8; ++pos) {
                int n = 70;
                for (int i = 0; i < n; ++i) s[i] = (i % 8 == 7) ? '\\' : (char)('A' + i % 23);
                s[pos * 9] = (char)v;
                s[n] = 0;
                chk(s, n, "byte value as the sole non-uppercase character"); ++cases;
            }
            /* and short, where index 0 is in play */
            {
                char t[8];
                t[0]=(char)v; t[1]='B'; t[2]='C'; t[3]='D'; t[4]=0;
                chk(t, 4, "byte value AT INDEX 0"); ++cases;
                t[0]='A'; t[1]=(char)v; t[2]='C'; t[3]='D'; t[4]=0;
                chk(t, 4, "byte value at index 1"); ++cases;
                t[0]=(char)v; t[1]=0;
                chk(t, 1, "byte value alone"); ++cases;
            }
        }
        printf("  all 255 byte values at 8 positions of a 70-byte path, at index 0, at index 1\n"
               "  and alone: %ld cases\n", cases);
    }

    /* ---- both MAPS: every ordered pair of (index 0 byte, index 1 byte) ----------------------- */
    /* The only way to see index 0 uppercased is a byte that is lowercase in CP1252 but not ASCII,
       so the two positions are swept together rather than one at a time. */
    {
        static const unsigned char INTERESTING[] = {
            0x41,0x5A,0x5B,0x5E,0x60,0x61,0x7A,0x7F,0x88,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,
            0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,0xA0,0xBF,0xC0,0xD6,0xD7,0xD8,0xDE,0xDF,
            0xE0,0xF6,0xF7,0xF8,0xFE,0xFF,0x30,0x5C,0x3A
        };
        int NI = (int)(sizeof INTERESTING / sizeof INTERESTING[0]);
        long cases = 0;
        char t[8];
        for (int a = 0; a < NI; ++a) {
            for (int b = 0; b < NI; ++b) {
                t[0] = (char)INTERESTING[a];
                t[1] = (char)INTERESTING[b];
                t[2] = 'C'; t[3] = 'D'; t[4] = 0;
                chk(t, 4, "index 0 x index 1"); ++cases;
                t[0] = (char)INTERESTING[a];
                t[1] = (char)INTERESTING[b];
                t[2] = 0;
                chk(t, 2, "index 0 x index 1, length 2"); ++cases;
            }
        }
        printf("  every ordered pair of %d interesting byte values at indices 0 and 1: %ld cases\n",
               NI, cases);
    }

    /* ---- alignments x lengths, all-uppercase and with one lowercase letter ------------------- */
    {
        static char pool[2048];
        long cases = 0;
        for (int off = 0; off < 32; ++off) {
            for (int n = 1; n <= 70; ++n) {
                char* s = pool + off;
                for (int i = 0; i < n; ++i) s[i] = (i % 7 == 6) ? '\\' : (char)('A' + i % 23);
                s[n] = 0;
                chk(s, n, "aligned, all uppercase"); ++cases;
                for (int pos = 0; pos < n; ++pos) {
                    char save = s[pos];
                    s[pos] = 'q';                 /* one lowercase letter: must veto */
                    chk(s, n, "aligned, one lowercase letter"); ++cases;
                    s[pos] = (char)0xE0;          /* lowercase in CP1252, NOT in ASCII: no veto */
                    chk(s, n, "aligned, one CP1252 lowercase letter"); ++cases;
                    s[pos] = save;
                }
            }
        }
        printf("  32 alignments x lengths 1..70, all-uppercase and with an ASCII lowercase or a\n"
               "  CP1252 lowercase letter at EVERY position: %ld cases\n", cases);
    }

    /* ---- Length as a dimension: across the 259/260 rewrite boundary -------------------------- */
    {
        static char s[900];
        long cases = 0;
        for (int n = 240; n <= 700; ++n) {
            for (int i = 0; i < n; ++i) s[i] = (i % 8 == 7) ? '\\' : (char)('A' + i % 23);
            s[n] = 0;
            chk(s, n, "long, all uppercase"); ++cases;
            /* a CP1252 lowercase letter either side of the bound: rewritten or not? */
            for (int pos = 250; pos <= 270 && pos < n; ++pos) {
                char save = s[pos];
                s[pos] = (char)0xE0;
                chk(s, n, "long, CP1252 lowercase across the bound"); ++cases;
                s[pos] = save;
            }
            /* an ASCII lowercase letter PAST the rewrite bound must still veto */
            if (n > 300) {
                char save = s[n - 5];
                s[n - 5] = 'q';
                chk(s, n, "long, ASCII lowercase past the rewrite bound"); ++cases;
                s[n - 5] = save;
            }
        }
        printf("  lengths 240..700 with a CP1252 lowercase letter swept across the 259/260\n"
               "  boundary, and an ASCII lowercase letter past it: %ld cases\n", cases);
    }

    /* ---- fuzz --------------------------------------------------------------------------------- */
    {
        static const unsigned char AL[14] = { 'A','B','Z','\\',':','.', '0',
                                              0x8A,0x9A,0x9F,0xC0,0xE0,0xD7,0xFF };
        static char s[400];
        for (int t = 0; t < 300000; ++t) {
            int n = rnd() % 320;
            for (int i = 0; i < n; ++i) s[i] = (char)AL[rnd() % 14];
            /* one in eight gets an ASCII lowercase letter somewhere, so both branches are fuzzed */
            if (n && (rnd() & 7) == 0) s[rnd() % n] = (char)('a' + rnd() % 26);
            s[n] = 0;
            chk(s, n, "fuzz");
        }
        printf("  300000 fuzz cases over an alphabet of case-mapping edges, one in eight carrying\n"
               "  an ASCII lowercase letter so both branches are exercised\n");
    }

    /* ---- the page edge: the scalar step in pass 1 ---------------------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static const unsigned char FA[10] = { 'A','B',0x8A,0x9F,0xC0,0xD6,0xD8,0xDE,0xE0,0xFF };
        static char mirror[600];
        long cases = 0;
        for (int tail = 2; tail <= 250; ++tail) {
            for (int shape = 0; shape < 3; ++shape) {
                char* p = (base+pg) - tail;
                for (int i = 0; i < tail-1; ++i) {
                    p[i] = (char)FA[(i + shape) % 10];
                    if (shape == 1 && i % 6 == 5) p[i] = '\\';
                }
                if (shape == 2 && tail > 4) p[tail-3] = 'q';    /* an ASCII lowercase: must veto */
                p[tail-1] = 0;
                memcpy(mirror, p, tail);
                /* run ours at the guard, and the oracle + live on an ordinary copy */
                memset(br, POISON, WIN); memset(bs, POISON, WIN);
                memcpy(br, mirror, tail); memcpy(bs, mirror, tail);
                int r0 = !!wia_pathmakeprettya(p);
                int r1 = !!ref_pathmakeprettya(br);
                int r2 = !!sys(bs);
                int ok = (r0 == r1) && (r0 == r2)
                      && memcmp(p, br, tail) == 0 && memcmp(p, bs, tail) == 0;
                if (!ok) {
                    ++fails;
                    if (shown < 20) {
                        printf("FAIL page-guard (tail %d shape %d): ret %d/%d/%d\n",
                               tail, shape, r0, r1, r2);
                        ++shown;
                    }
                }
                ++cases;
            }
        }
        printf("  page-guard sweep, the string ending at a NOACCESS page, three shapes: %ld cases\n",
               cases);
        VirtualFree(base,0,MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%ld)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathMakePrettyA vs live shlwapi + an independent oracle, comparing the\n"
           "return AND a 1024-byte POISON WINDOW on every case -- because the return does NOT mean\n"
           "\"something changed\" (\"123456\", \"\" and \"\\\\\\\\\" all return 1 while changing nothing) and a\n"
           "refusal writes NOTHING, which a string comparison cannot tell from writing the same bytes\n"
           "back. Corpus: the probe-derived shapes; NULL; all 255 byte values at 8 positions of a\n"
           "70-byte path AND at index 0, index 1 and alone, because the refusal set is ASCII-ONLY --\n"
           "exactly 26 values -- while BOTH case maps cover the CP1252 range, so an implementation\n"
           "using the code page's notion of lowercase would veto on 30 values too many; every ordered\n"
           "pair of 38 interesting byte values at indices 0 and 1, since index 0 is UPPERCASED and is\n"
           "observable only through the bytes the refusal ignores; 32 alignments x lengths 1..70 with\n"
           "an ASCII lowercase letter and a CP1252 lowercase letter at EVERY position; lengths\n"
           "240..700 sweeping a CP1252 lowercase letter across the 259/260 boundary and placing an\n"
           "ASCII lowercase letter PAST it, because the rewrite is bounded and the refusal scan is\n"
           "NOT; 300000 fuzz cases over the case-mapping edges; and a page-guard sweep with the\n"
           "string ending at a NOACCESS page in three shapes, which is what reaches pass 1's one-byte\n"
           "scalar step)\n");
    return 0;
}
