// changes/234-pathfindnextcomponenta/correctness.c
// Gate 1: wia_pathfindnextcomponenta must be indistinguishable from shlwapi!PathFindNextComponentA.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// This function writes nothing, so the whole contract is the RETURNED POINTER, compared as an
// offset, with NULL distinguished from "a pointer to the terminator". Those two are easy to
// conflate and the export uses both: an empty string gives NULL, a string with no separator gives
// the terminator.
//
// The runs are enumerated on purpose. The doubled-separator rule advances exactly one more, never
// the whole run, so "skip the separators" (the obvious implementation) is correct on one and
// two backslashes and wrong from three onward. A corpus that only ever puts single separators
// between components cannot see that, which is why the alphabet below is mostly separators and the
// lengths run to 9.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern char* wia_pathfindnextcomponenta(const char*);
char* ref_pathfindnextcomponenta(const char*);
typedef char* (WINAPI *FN)(const char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static unsigned long sd = 0x3141u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* NULL is -1; anything else is an offset from the start */
static long off(const char* p, char* r){ return r ? (long)(r - p) : -1; }

static int chk(const char* s, const char* what)
{
    long a = off(s, wia_pathfindnextcomponenta(s));
    long b = off(s, ref_pathfindnextcomponenta(s));
    long c = off(s, sys(s));
    int ok = (a == b) && (a == c);
    if (!ok && fails < 15)
        printf("FAIL: %s -- \"%s\": ours %ld oracle %ld live %ld\n", what, s, a, b, c);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathFindNextComponentA");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathFindNextComponentA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char s[700];

    // probe-derived cases
    {
        static const char* V[] = {
            "", "a", "ab", "\\", "\\\\", "\\\\\\", "\\\\\\\\", "a\\b", "a\\\\b", "a\\\\\\b",
            "\\a", "\\\\a", "\\\\\\a", "C:\\dir\\file", "a/b", "//", "a\\", "ab\\\\",
            "\\\\server\\share", 0
        };
        for (int i = 0; V[i]; ++i) chk(V[i], "probe-derived case");
    }

    // ---- SEPARATOR RUNS at every length and every position ---------------------------------------
    // The rule advances exactly one extra, so the answer must stop moving once the run reaches two.
    for (int lead = 0; lead <= 6; ++lead) {
        for (int run = 1; run <= 10; ++run) {
            int k = 0;
            for (int i = 0; i < lead; ++i) s[k++] = 'a';
            for (int i = 0; i < run; ++i) s[k++] = '\\';
            s[k++] = 'x'; s[k++] = 'y'; s[k] = 0;
            chk(s, "separator run");
            /* and with the run at the very end, so the byte after it is the terminator */
            k = 0;
            for (int i = 0; i < lead; ++i) s[k++] = 'a';
            for (int i = 0; i < run; ++i) s[k++] = '\\';
            s[k] = 0;
            chk(s, "separator run at the end");
        }
    }

    // ---- every byte value: only 0x5C may be a separator ------------------------------------------
    for (int v = 1; v < 256; ++v) {
        s[0]='a'; s[1]=(char)v; s[2]='b'; s[3]=0;   chk(s, "byte value in the middle");
        s[0]=(char)v; s[1]='b'; s[2]=0;             chk(s, "byte value first");
        s[0]='a'; s[1]=(char)v; s[2]=0;             chk(s, "byte value last");
        /* and immediately after a separator, where the doubled rule looks */
        s[0]='a'; s[1]='\\'; s[2]=(char)v; s[3]='b'; s[4]=0;
        chk(s, "byte value after a separator");
    }

    // ---- EXHAUSTIVE over the alphabet that reaches every branch -----------------------------------
    {
        static const char AL[4] = { 'a', '\\', '/', (char)0x80 };
        long en = 0;
        for (int n = 0; n <= 9; ++n) {
            long lim = 1; for (int i = 0; i < n; ++i) lim *= 4;
            for (long k = 0; k < lim; ++k) {
                long v = k;
                for (int i = 0; i < n; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[n] = 0;
                chk(s, "exhaustive {a,backslash,/,0x80} 0..9");
                ++en;
            }
        }
        printf("  exhaustive {a,backslash,/,0x80} 0..9: %ld strings\n", en);
    }

    // ---- long strings and alignments ---------------------------------------------------------------
    {
        static char buf[700];
        for (int o = 0; o < 32; ++o) {
            for (int n = 1; n <= 130; ++n) {
                char* p = buf + o;
                for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
                p[n] = 0;
                chk(p, "long, no separator");
                if (n > 4) {
                    p[n/2] = '\\';
                    chk(p, "long, one separator");
                    if (n/2 + 1 < n) { p[n/2+1] = '\\'; chk(p, "long, doubled separator"); }
                }
            }
        }
        for (int n = 200; n <= 600; n += 19) {
            for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
            s[n] = 0;
            chk(s, "very long, no separator");
            s[n-1] = '\\';
            chk(s, "very long, separator at the end");
        }
    }

    // ---- NULL ----------------------------------------------------------------------------------------
    CHECK(wia_pathfindnextcomponenta(0) == 0, "NULL returns NULL");
    CHECK(ref_pathfindnextcomponenta(0) == 0, "NULL returns NULL (oracle)");
    CHECK(sys(0) == 0,                        "NULL returns NULL (live)");

    // ---- fuzz -----------------------------------------------------------------------------------------
    {
        static const char AL[8] = { 'a', '\\', '\\', '/', ':', 'b', (char)0x80, (char)0xFF };
        for (int t = 0; t < 400000; ++t) {
            int n = rnd() % 80;
            for (int i = 0; i < n; ++i) s[i] = AL[rnd() % 8];
            s[n] = 0;
            chk(s, "fuzz");
        }
    }

    // ---- NOACCESS page guard: the scan must stop at the terminator ----------------------------------
    // Including the case where a SEPARATOR is the last character before it, because the rule then
    // reads one byte further; that byte is the terminator, and it must not read past it.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[700];
        for (int tail = 2; tail <= 200; ++tail) {
            for (int shape = 0; shape < 3; ++shape) {
                char* p = (base+pg) - tail;
                for (int i = 0; i < tail-1; ++i) p[i] = (char)('a' + i % 23);
                if (shape == 1) p[tail-2] = '\\';                    /* separator at the edge */
                if (shape == 2 && tail >= 4) { p[tail-3] = '\\'; p[tail-2] = '\\'; }
                p[tail-1] = 0;
                int n = 0; while (p[n]) { b[n] = p[n]; ++n; } b[n] = 0;
                long a = off(p, wia_pathfindnextcomponenta(p));      // must not read into page 2
                long r = off(b, ref_pathfindnextcomponenta(b));
                CHECK(a == r, "page-guard: same offset");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathFindNextComponentA vs live shlwapi + oracle, comparing the "
           "RETURNED POINTER as an offset with NULL kept distinct from a pointer to the terminator "
           "(the export uses both): probe-derived cases, SEPARATOR RUNS of length 1..10 at 7 lead "
           "positions and at the end -- because the rule advances exactly ONE extra and 'skip the "
           "run' is only wrong from three onward -- ALL 255 byte values at four positions including "
           "immediately after a separator, exhaustive {a,backslash,/,0x80} to length 9 (349525 "
           "strings), 32 alignments x lengths 1..130 in three shapes, very long strings to 600, "
           "NULL, 400k fuzz over a separator-heavy alphabet, and a NOACCESS page-guard sweep whose "
           "shapes include a separator as the last character, where the rule reads one byte "
           "further)\n");
    return 0;
}
