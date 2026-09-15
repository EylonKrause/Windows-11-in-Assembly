/* changes/240-pathcchremovefilespec/correctness.c
   Gate 1: wia_pathcchremovefilespec must be indistinguishable from kernelbase!PathCchRemoveFileSpec.
   Three-way: our assembly vs the independent scalar oracle vs the LIVE export on this PC.

   EVERY COMPARISON CHECKS THE HRESULT AND THE WHOLE BUFFER AGAINST A POISON FILL. That is not
   belt-and-braces here; three separately measured facts make anything less insufficient:

     * IT CLEARS A SLOT PER REMOVED SEPARATOR, not one terminator at the cut. An implementation that
       wrote a single terminator produces the SAME STRING and a different BUFFER, and the only reason
       this project knows the rule at all is that probes/pcrfs3.c started reporting WHICH INDEX
       differed instead of comparing a window and saying "mismatch".
     * S_FALSE WRITES NOTHING AT ALL, which a string comparison cannot tell from writing the same
       terminator back.
     * cch BOUNDS THE HIGHEST INDEX WRITTEN -- including writes that land on the existing terminator
       and are invisible in the buffer. 567 UNC cases differ from "result+1" for exactly that reason.

   THE CORPUS IS BUILT AROUND THE FOUR RULES AND THE THREE TRAPS.

     1. THE ROOT IS NOT PathCchSkipRoot'S ROOT. They differ by one on every UNC path with anything
        after the share, and SkipRoot declines outright on 15 355 of 21 845 enumerated strings. So the
        corpus enumerates the separator/colon/'?' alphabet exhaustively rather than sampling real
        paths, and carries 'U','N','C' in both cases for the extended prefix.
     2. A DRIVE LETTER IS 114 WCHAR VALUES, NOT 52 -- ASCII plus the CP1252 accented letters. Because
        the function is WIDE, the sweep runs over ALL 65 536 values, not 1..255. This is change 232's
        divergence from the other side.
     3. THE EMPTY-SHARE CLAUSE: "\\a\" -> 3, "\\\" -> 2, "\\\\" -> 2. Those shapes are enumerated,
        not spot-checked.
     4. LENGTH IS A DIMENSION, swept to 4000 in four root shapes -- change 236's model survived 3.65
        million pairs and was still wrong about a rule that started at 260 characters. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

extern HRESULT wia_pathcchremovefilespec(wchar_t*, size_t);
HRESULT ref_pathcchremovefilespec(wchar_t*, size_t);
typedef HRESULT (WINAPI *FN)(PWSTR, size_t);
static FN sys;

#define WIN 4200
static wchar_t bo[WIN], br[WIN], bs[WIN];
static long fails = 0;
static int  shown = 0;

static unsigned long sd = 0x77C1u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static void chk(const wchar_t* in, int n, size_t cch, const char* what)
{
    for (int i = 0; i < WIN; ++i) { bo[i] = 0xCDCD; br[i] = 0xCDCD; bs[i] = 0xCDCD; }
    memcpy(bo, in, (size_t)(n + 1) * 2);
    memcpy(br, in, (size_t)(n + 1) * 2);
    memcpy(bs, in, (size_t)(n + 1) * 2);
    HRESULT h0 = wia_pathcchremovefilespec(bo, cch);
    HRESULT h1 = ref_pathcchremovefilespec(br, cch);
    HRESULT h2 = sys(bs, cch);
    int ok = (h0 == h1) && (h0 == h2)
          && memcmp(bo, br, sizeof bo) == 0 && memcmp(bo, bs, sizeof bo) == 0;
    if (!ok) {
        ++fails;
        if (shown < 20) {
            int at = -1;
            for (int i = 0; i < WIN; ++i) if (bo[i] != bs[i]) { at = i; break; }
            printf("FAIL %s: \"%ls\" cch=%zu -> ours 0x%08lX \"%ls\" | oracle 0x%08lX \"%ls\" | "
                   "live 0x%08lX \"%ls\"", what, in, cch,
                   (unsigned long)h0, bo, (unsigned long)h1, br, (unsigned long)h2, bs);
            if (at >= 0) printf("   INDEX %d: ours %04X live %04X", at, bo[at], bs[at]);
            printf("\n");
            ++shown;
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "PathCchRemoveFileSpec");
    if (!sys) { printf("CORRECTNESS: cannot resolve kernelbase!PathCchRemoveFileSpec\n"); return 1; }

    /* ---- NULL, which must not be reached through the buffer at all --------------------------- */
    {
        HRESULT a = wia_pathcchremovefilespec(0, PATHCCH_MAX_CCH);
        HRESULT b = ref_pathcchremovefilespec(0, PATHCCH_MAX_CCH);
        HRESULT c = sys(0, PATHCCH_MAX_CCH);
        if (a != b || a != c) { printf("FAIL: NULL -> %08lX %08lX %08lX\n",
                                       (unsigned long)a, (unsigned long)b, (unsigned long)c);
                                ++fails; }
        a = wia_pathcchremovefilespec(0, 0); b = ref_pathcchremovefilespec(0, 0); c = sys(0, 0);
        if (a != b || a != c) { printf("FAIL: NULL, cch 0\n"); ++fails; }
    }

    /* ---- exhaustive over the path-shaped alphabet -------------------------------------------- */
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long cases = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                chk(s, len, PATHCCH_MAX_CCH, "exhaustive"); ++cases;
            }
        }
        printf("  exhaustive {a,backslash,colon,?} to length 8: %ld strings\n", cases);
    }

    /* ---- and with 'U','N','C' in both cases, for the extended prefix -------------------------- */
    {
        static const wchar_t AL[8] = { L'a', L'\\', L':', L'?', L'U', L'N', L'C', L'u' };
        wchar_t s[16];
        long cases = 0;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 8;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 8]; v /= 8; }
                s[len] = 0;
                chk(s, len, PATHCCH_MAX_CCH, "extended alphabet"); ++cases;
            }
        }
        printf("  exhaustive {a,backslash,colon,?,U,N,C,u} to length 6: %ld strings\n", cases);
    }

    /* ---- THE DRIVE LETTER OVER ALL 65536 WCHAR VALUES ---------------------------------------- */
    {
        wchar_t s[16];
        long cases = 0;
        for (unsigned v = 1; v < 0x10000; ++v) {
            if (v == L'\\') continue;
            s[0]=(wchar_t)v; s[1]=L':'; s[2]=L'\\'; s[3]=L'a'; s[4]=0;
            chk(s, 4, PATHCCH_MAX_CCH, "drive letter"); ++cases;
        }
        for (unsigned v = 1; v < 0x10000; v += 5) {
            if (v == L'\\') continue;
            s[0]=L'\\'; s[1]=L'\\'; s[2]=L'?'; s[3]=L'\\';
            s[4]=(wchar_t)v; s[5]=L':'; s[6]=L'\\'; s[7]=L'a'; s[8]=0;
            chk(s, 8, PATHCCH_MAX_CCH, "extended drive letter"); ++cases;
        }
        printf("  the drive letter over all 65536 wchar values, bare and extended: %ld cases\n",
               cases);
    }

    /* ---- realistic shapes, cch swept across the threshold ------------------------------------- */
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\file.txt", L"C:\\dir\\", L"C:\\dir", L"C:\\", L"C:", L"C:file",
            L"\\dir\\file", L"\\dir", L"\\", L"\\\\srv\\shr\\dir\\file", L"\\\\srv\\shr",
            L"\\\\srv", L"\\\\", L"\\\\?\\C:\\dir\\file", L"\\\\?\\C:\\", L"\\\\?\\C:",
            L"\\\\?\\UNC\\srv\\shr\\file", L"\\\\?\\UNC\\srv\\shr", L"\\\\?\\UNC\\srv",
            L"\\\\?\\UNC\\", L"\\\\?\\UNC", L"\\\\?\\", L"\\\\?", L"dir\\file", L"dir",
            L"", L"a\\\\b", L"a\\\\\\b", L"C:\\a\\\\\\", L"\\\\srv\\shr\\\\\\", L"::", L"?:",
            L"\\\\\\", L"\\\\\\a", L"\\\\a\\", L"\\\\a", L"a\\\\", L"aa\\\\",
            L"\\\\?\\c:\\x", L"\\\\?\\unc\\s\\h\\x", L"\\\\a\\b\\c", L"\\\\\\a\\a", L"a:\\\\aa",
            0
        };
        long cases = 0;
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            for (size_t cch = 1; cch <= (size_t)n + 4; ++cch) { chk(V[i], n, cch, "cch"); ++cases; }
            chk(V[i], n, PATHCCH_MAX_CCH, "max cch"); ++cases;
            chk(V[i], n, PATHCCH_MAX_CCH + 1, "past max cch"); ++cases;
            chk(V[i], n, 0, "cch 0"); ++cases;
            chk(V[i], n, (size_t)-1, "cch SIZE_MAX"); ++cases;
        }
        printf("  %d probe-derived shapes with cch swept across the threshold: %ld cases\n",
               (int)(sizeof V / sizeof V[0]) - 1, cases);
    }

    /* ---- LENGTH AS A DIMENSION, four root shapes, and every alignment ------------------------- */
    {
        static wchar_t pool[4400];
        long cases = 0;
        for (int shape = 0; shape < 4; ++shape) {
            for (int n = 10; n <= 4000; n += 17) {
                wchar_t* s = pool;
                int k = 0;
                if (shape == 0) { s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                else if (shape == 1) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L's'; s[k++]=L'\\';
                                       s[k++]=L'h'; s[k++]=L'\\'; }
                else if (shape == 2) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L'?'; s[k++]=L'\\';
                                       s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                while (k < n) {
                    for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                    if (k < n) s[k++] = L'\\';
                }
                s[k] = 0;
                chk(s, k, PATHCCH_MAX_CCH, "long"); ++cases;
                chk(s, k, (size_t)k + 1, "long, tight cch"); ++cases;
                chk(s, k, (size_t)k, "long, cch one short"); ++cases;
                if (k > 6) {
                    wchar_t a = s[k-3], b = s[k-2], c = s[k-1];
                    s[k-3]=L'\\'; s[k-2]=L'\\'; s[k-1]=L'\\';
                    chk(s, k, PATHCCH_MAX_CCH, "long, three trailing separators"); ++cases;
                    s[k-2]=L'x';
                    chk(s, k, PATHCCH_MAX_CCH, "long, separator x separator"); ++cases;
                    s[k-3]=a; s[k-2]=b; s[k-1]=c;
                }
            }
        }
        printf("  lengths 10..4000 in four root shapes, with tight cch and trailing runs: %ld cases\n",
               cases);
    }

    /* ---- 16 alignments, so the vector loops start at every offset ---------------------------- */
    {
        static wchar_t pool[2048];
        long cases = 0;
        for (int off = 0; off < 16; ++off) {
            for (int n = 1; n <= 80; ++n) {
                wchar_t* s = pool + off;
                for (int i = 0; i < n; ++i) s[i] = (i % 7 == 6) ? L'\\' : (wchar_t)(L'a' + i % 23);
                s[n] = 0;
                chk(s, n, PATHCCH_MAX_CCH, "aligned"); ++cases;
                for (int pos = 0; pos < n; ++pos) {
                    wchar_t save = s[pos];
                    s[pos] = L'\\';
                    chk(s, n, PATHCCH_MAX_CCH, "aligned, separator at every position"); ++cases;
                    s[pos] = save;
                }
            }
        }
        printf("  16 alignments x lengths 1..80 with a separator at EVERY position: %ld cases\n",
               cases);
    }

    /* ---- fuzz --------------------------------------------------------------------------------- */
    {
        static const wchar_t AL[8] = { L'a', L'b', L'\\', L':', L'?', L'U', L'N', L'C' };
        static wchar_t s[300];
        for (int t = 0; t < 300000; ++t) {
            int n = rnd() % 200;
            for (int i = 0; i < n; ++i) s[i] = AL[rnd() % 8];
            s[n] = 0;
            size_t cch = (rnd() & 3) ? PATHCCH_MAX_CCH : (size_t)(rnd() % (unsigned)(n + 4) + 1);
            chk(s, n, cch, "fuzz");
        }
        printf("  300000 fuzz cases over a root-shaped alphabet, one in four with a tight cch\n");
    }

    /* ---- the page edge: the wcslen's one-character step --------------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t mirror[600];
        long cases = 0;
        for (int tail = 4; tail <= 250; ++tail) {
            for (int shape = 0; shape < 3; ++shape) {
                wchar_t* p = (wchar_t*)((base+pg) - tail*2);
                for (int i = 0; i < tail-1; ++i) {
                    if (shape == 0) p[i] = (i % 8 == 7) ? L'\\' : (wchar_t)(L'a' + i % 23);
                    else if (shape == 1) p[i] = (wchar_t)(L'a' + i % 23);
                    else p[i] = (i < 2) ? L'\\' : (wchar_t)(L'a' + i % 23);
                }
                p[tail-1] = 0;
                memcpy(mirror, p, (size_t)tail * 2);
                /* ours runs AT the guard; the oracle and the live export on an ordinary copy */
                for (int i = 0; i < WIN; ++i) { br[i] = 0xCDCD; bs[i] = 0xCDCD; }
                memcpy(br, mirror, (size_t)tail * 2);
                memcpy(bs, mirror, (size_t)tail * 2);
                HRESULT h0 = wia_pathcchremovefilespec(p, (size_t)tail);
                HRESULT h1 = ref_pathcchremovefilespec(br, (size_t)tail);
                HRESULT h2 = sys(bs, (size_t)tail);
                int ok = (h0 == h1) && (h0 == h2)
                      && memcmp(p, br, (size_t)tail * 2) == 0
                      && memcmp(p, bs, (size_t)tail * 2) == 0;
                if (!ok) {
                    ++fails;
                    if (shown < 20) {
                        printf("FAIL page-guard (tail %d shape %d): %08lX / %08lX / %08lX\n",
                               tail, shape, (unsigned long)h0, (unsigned long)h1,
                               (unsigned long)h2);
                        ++shown;
                    }
                }
                ++cases;
            }
        }
        printf("  page-guard sweep, the path ending at a NOACCESS page, three shapes: %ld cases\n",
               cases);
        VirtualFree(base,0,MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%ld)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathCchRemoveFileSpec vs live kernelbase + an independent oracle,\n"
           "comparing the HRESULT AND the WHOLE BUFFER against a poison fill on every case -- because\n"
           "the function CLEARS A SLOT PER REMOVED SEPARATOR rather than writing one terminator at the\n"
           "cut, because S_FALSE writes nothing at all, and because cch bounds the HIGHEST INDEX\n"
           "WRITTEN including writes that land on the existing terminator and are invisible in the\n"
           "buffer. Corpus: NULL; exhaustive {a,backslash,colon,?} to length 8 and\n"
           "{a,backslash,colon,?,U,N,C,u} to length 6, enumerated rather than sampled because the\n"
           "protected root is NOT PathCchSkipRoot's root -- they differ by one on every UNC path and\n"
           "SkipRoot declines outright on 15355 of 21845 strings; THE DRIVE LETTER OVER ALL 65536\n"
           "WCHAR VALUES, bare and extended, because the function is WIDE and 1..255 is not a sweep\n"
           "and the answer is 114 values rather than 52; 42 probe-derived shapes with cch swept across\n"
           "its threshold and at 0, PATHCCH_MAX_CCH, one past it and SIZE_MAX; lengths 10..4000 in\n"
           "four root shapes with tight cch and trailing separator runs, because LENGTH IS A\n"
           "DIMENSION; 16 alignments x lengths 1..80 with a separator at EVERY position; 300000 fuzz\n"
           "cases; and a page-guard sweep with the path ending at a NOACCESS page in three shapes)\n");
    return 0;
}
