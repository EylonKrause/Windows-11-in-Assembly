// changes/235-pathisfilespeca/correctness.c
// Gate 1: wia_pathisfilespeca must be indistinguishable from shlwapi!PathIsFileSpecA.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// The function writes nothing, so the whole contract is the BOOL. Two things make it worth testing
// harder than that sounds:
//
//   * There are two separators, not one. 0x5C and 0x3A. The colon is the one a reader forgets, so
//     every byte value is swept at three positions rather than spot-checked.
//   * The empty string is TRUE. That is the single case a natural model gets wrong -- the probe's
//     first model required a non-empty string and that was its only mismatch in 488281 strings --
//     so it is asserted directly as well as covered by the enumeration.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_pathisfilespeca(const char*);
int ref_pathisfilespeca(const char*);
typedef int (WINAPI *FN)(const char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static unsigned long sd = 0x2718u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int chk(const char* s, const char* what)
{
    int a = !!wia_pathisfilespeca(s);
    int b = !!ref_pathisfilespeca(s);
    int c = !!sys(s);
    int ok = (a == b) && (a == c);
    if (!ok && fails < 15)
        printf("FAIL: %s -- \"%s\": ours %d oracle %d live %d\n", what, s, a, b, c);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathIsFileSpecA");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathIsFileSpecA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char s[700];

    // ---- The empty string, asserted directly ------------------------------------------------------
    CHECK(wia_pathisfilespeca("") == 1, "the EMPTY STRING is TRUE");
    CHECK(ref_pathisfilespeca("") == 1, "the EMPTY STRING is TRUE (oracle)");
    CHECK(sys("") == 1,                 "the EMPTY STRING is TRUE (live)");

    // probe-derived cases
    {
        static const char* V[] = { "", "a", "file.txt", "a\\b", "\\", ":", "a:b", "C:",
                                   "dir\\file", "a/b", "/", "..", ".", "\\\\", "::",
                                   "C:\\dir\\file.txt", 0 };
        for (int i = 0; V[i]; ++i) chk(V[i], "probe-derived case");
    }

    // ---- every byte value, at the first, middle and last positions --------------------------------
    for (int v = 1; v < 256; ++v) {
        s[0]=(char)v; s[1]='b'; s[2]='c'; s[3]=0;   chk(s, "byte value first");
        s[0]='a'; s[1]=(char)v; s[2]='c'; s[3]=0;   chk(s, "byte value middle");
        s[0]='a'; s[1]='b'; s[2]=(char)v; s[3]=0;   chk(s, "byte value last");
        s[0]=(char)v; s[1]=0;                        chk(s, "byte value alone");
        /* and deep inside a long string, past the first 32-byte block */
        for (int i = 0; i < 100; ++i) s[i] = 'a';
        s[70] = (char)v; s[100] = 0;
        chk(s, "byte value at offset 70");
    }

    // ---- EXHAUSTIVE over the alphabet that reaches every branch ------------------------------------
    {
        static const char AL[5] = { 'a', '\\', ':', '/', (char)0x80 };
        long en = 0;
        for (int n = 0; n <= 8; ++n) {
            long lim = 1; for (int i = 0; i < n; ++i) lim *= 5;
            for (long k = 0; k < lim; ++k) {
                long v = k;
                for (int i = 0; i < n; ++i) { s[i] = AL[v % 5]; v /= 5; }
                s[n] = 0;
                chk(s, "exhaustive {a,backslash,:,/,0x80} 0..8");
                ++en;
            }
        }
        printf("  exhaustive {a,backslash,:,/,0x80} 0..8: %ld strings\n", en);
    }

    // ---- alignments x lengths, with the separator at every position --------------------------------
    {
        static char buf[700];
        for (int o = 0; o < 32; ++o) {
            for (int n = 1; n <= 70; ++n) {
                char* p = buf + o;
                for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
                p[n] = 0;
                chk(p, "alignment x length, clean");
                for (int pos = 0; pos < n; ++pos) {
                    char save = p[pos];
                    p[pos] = '\\';  chk(p, "separator at every position");
                    p[pos] = ':';   chk(p, "colon at every position");
                    p[pos] = save;
                }
            }
        }
    }

    // ---- long strings ------------------------------------------------------------------------------
    for (int n = 200; n <= 600; n += 13) {
        for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
        s[n] = 0;
        chk(s, "long, clean");
        s[n-1] = '\\'; chk(s, "long, separator at the end");
        s[n-1] = (char)('a' + (n-1) % 23);
        s[0] = ':';    chk(s, "long, colon at the front");
        s[0] = 'a';
    }

    // ---- NULL ----------------------------------------------------------------------------------------
    CHECK(wia_pathisfilespeca(0) == 0, "NULL returns 0");
    CHECK(ref_pathisfilespeca(0) == 0, "NULL returns 0 (oracle)");
    CHECK(sys(0) == 0,                 "NULL returns 0 (live)");

    // ---- fuzz -----------------------------------------------------------------------------------------
    {
        static const char AL[8] = { 'a', 'b', '\\', ':', '/', '.', (char)0x80, (char)0xFF };
        for (int t = 0; t < 400000; ++t) {
            int n = rnd() % 120;
            for (int i = 0; i < n; ++i) s[i] = AL[rnd() % 8];
            s[n] = 0;
            chk(s, "fuzz");
        }
    }

    // ---- NOACCESS page guard, in both shapes --------------------------------------------------------
    // A clean string ending at the guard (the scan must run to the terminator and stop), and one
    // with a separator at the front (where an implementation may stop early but must not overread
    // either way).
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
                if (shape == 1) p[0] = '\\';
                if (shape == 2) p[tail-2] = ':';
                p[tail-1] = 0;
                int n = 0; while (p[n]) { b[n] = p[n]; ++n; } b[n] = 0;
                int a = !!wia_pathisfilespeca(p);     // must not read into page 2
                int r = !!ref_pathisfilespeca(b);
                CHECK(a == r, "page-guard: same result");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathIsFileSpecA vs live shlwapi + oracle: THE EMPTY STRING IS TRUE "
           "asserted directly (the one case a natural model gets wrong), ALL 255 byte values at the "
           "first, middle and last positions, alone, and at offset 70 past the first block -- "
           "because there are TWO separators, 0x5C and 0x3A, and the colon is the one a reader "
           "forgets; exhaustive {a,backslash,:,/,0x80} to length 8 (488281 strings), 32 alignments "
           "x lengths 1..70 with BOTH separators placed at EVERY position, long strings to 600, "
           "NULL, 400k fuzz, and a NOACCESS page-guard sweep in three shapes)\n");
    return 0;
}
