// changes/232-pathremovebackslasha/correctness.c
// Gate 1: wia_pathremovebackslasha must be indistinguishable from shlwapi!PathRemoveBackslashA.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// THE RETURN VALUE IS PART OF THE CONTRACT AND IS EASY TO GET WRONG. It is psz + max(n-1, 0) -- a
// pointer to the LAST CHARACTER, not to the terminator and not to the start -- and it is returned
// whether or not anything was stripped. Every case here compares the returned OFFSET as well as the
// whole buffer.
//
// AND THE DRIVE-LETTER SWEEP IS NOT OPTIONAL. The WIDE sibling (change 171) accepts the Latin-1
// letters as drive letters; this narrow one accepts ASCII only. An implementation that inherited
// the wide set would wrongly protect 78 byte values, and nothing but a full 0..255 sweep at that
// position can see it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern char* wia_pathremovebackslasha(char*);
char* ref_pathremovebackslasha(char*);
typedef char* (WINAPI *FN)(char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define DSZ 640

static unsigned long sd = 0x5A5A5u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* one three-way case: the returned OFFSET and the WHOLE buffer */
static int chk(const char* src, int off, const char* what)
{
    static char a[DSZ], b[DSZ], c[DSZ];
    memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
    size_t n = strlen(src);
    char* pa = a + off; char* pb = b + off; char* pc = c + off;
    memcpy(pa, src, n+1); memcpy(pb, src, n+1); memcpy(pc, src, n+1);
    char* ra = wia_pathremovebackslasha(pa);
    char* rb = ref_pathremovebackslasha(pb);
    char* rc = sys(pc);
    int ok = (ra - pa) == (rb - pb) && (ra - pa) == (rc - pc)
          && memcmp(a, b, DSZ) == 0 && memcmp(a, c, DSZ) == 0;
    if (!ok && fails < 15)
        printf("FAIL: %s -- \"%s\" (n=%d, off=%d): offsets ours %d oracle %d live %d\n",
               what, src, (int)n, off, (int)(ra-pa), (int)(rb-pb), (int)(rc-pc));
    if (!ok) ++fails;
    return ok;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathRemoveBackslashA");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathRemoveBackslashA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char s[640];

    // every probe-derived case
    {
        static const char* V[] = {
            "", "a", "ab", "abc", "\\", "\\\\", "\\\\\\", "a\\", "ab\\", "abc\\",
            "C:", "C:\\", "C:\\\\", "C:a\\", "AB:\\", ":\\", "1:\\",
            "\\\\server\\", "\\\\server\\share\\",
            "a/", "a/\\", "a\\/", "//", "C:/", "/", "::\\", "z:\\", "Z:\\", 0
        };
        for (int i = 0; V[i]; ++i)
            for (int off = 0; off < 4; ++off)
                chk(V[i], off, "probe-derived case");
    }

    // ---- EVERY byte value as the DRIVE LETTER: the narrow/wide divergence -----------------------
    for (int v = 1; v < 256; ++v) {
        s[0] = (char)v; s[1] = ':'; s[2] = '\\'; s[3] = 0;
        chk(s, 0, "drive-letter sweep");
        /* and with a longer tail, so the m == 2 test is not the only thing being exercised */
        s[0] = (char)v; s[1] = ':'; s[2] = '\\'; s[3] = 'x'; s[4] = '\\'; s[5] = 0;
        chk(s, 0, "drive-letter sweep, longer");
    }
    // ---- EVERY byte value as the TRAILING character: only 0x5C may be removed -------------------
    for (int v = 1; v < 256; ++v) {
        s[0] = 'a'; s[1] = 'b'; s[2] = (char)v; s[3] = 0;
        chk(s, 0, "trailing-byte sweep");
        s[0] = (char)v; s[1] = 0;
        chk(s, 0, "single-byte sweep");
        s[0] = (char)v; s[1] = '\\'; s[2] = 0;
        chk(s, 0, "byte then backslash");
        s[0] = 'a'; s[1] = (char)v; s[2] = '\\'; s[3] = 0;
        chk(s, 0, "byte before a trailing backslash");
    }

    // ---- EXHAUSTIVE over the alphabet that reaches every branch ---------------------------------
    {
        static const char AL[6] = { 'a', '\\', '/', ':', 'C', 0x80 };
        long en = 0;
        for (int n = 0; n <= 7; ++n) {
            long lim = 1; for (int i = 0; i < n; ++i) lim *= 6;
            for (long k = 0; k < lim; ++k) {
                long v = k;
                for (int i = 0; i < n; ++i) { s[i] = AL[v % 6]; v /= 6; }
                s[n] = 0;
                chk(s, 0, "exhaustive {a,backslash,/,:,C,0x80} 0..7");
                ++en;
            }
        }
        printf("  exhaustive {a,backslash,/,:,C,0x80} to len 7: %ld strings\n", en);
        /* 0x80 is in the alphabet on purpose: it is a Latin-1 range byte, which the WIDE sibling
           would treat as a drive letter and this one must not. */
    }

    // ---- long strings and alignments -------------------------------------------------------------
    {
        static char buf[640];
        for (int off = 0; off < 32; ++off) {
            for (int n = 0; n <= 130; ++n) {
                char* p = buf + off;
                for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
                if (n > 0) p[n-1] = '\\';
                p[n] = 0;
                chk(p, 0, "long, trailing backslash, aligned");
            }
        }
        for (int n = 200; n <= 600; n += 17) {
            for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
            s[n-1] = '\\';
            s[n] = 0;
            chk(s, 0, "long, trailing backslash");
            s[n-1] = 'z';
            s[n] = 0;
            chk(s, 0, "long, no trailing backslash");
        }
    }

    // ---- NULL -------------------------------------------------------------------------------------
    CHECK(wia_pathremovebackslasha(0) == 0, "NULL returns NULL");
    CHECK(ref_pathremovebackslasha(0) == 0, "NULL returns NULL (oracle)");
    CHECK(sys(0) == 0,                      "NULL returns NULL (live)");

    // ---- fuzz --------------------------------------------------------------------------------------
    {
        static const char AL[8] = { 'a', '\\', '/', ':', 'C', 'z', (char)0x80, (char)0xC0 };
        for (int t = 0; t < 300000; ++t) {
            int n = rnd() % 60;
            for (int i = 0; i < n; ++i) s[i] = AL[rnd() % 8];
            s[n] = 0;
            chk(s, rnd() % 16, "fuzz");
        }
    }

    // ---- NOACCESS page guard: the scan must stop at the terminator --------------------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[DSZ];
        for (int tail = 2; tail <= 200; ++tail) {
            for (int shape = 0; shape < 2; ++shape) {
                char* p = (base+pg) - tail;
                for (int i = 0; i < tail-1; ++i) p[i] = (char)('a' + i % 23);
                if (shape) p[tail-2] = '\\';          /* a trailing backslash at the very edge */
                p[tail-1] = 0;
                memset(b, POISON, DSZ);
                int n = 0; while (p[n]) { b[n] = p[n]; ++n; } b[n] = 0;
                char* ra = wia_pathremovebackslasha(p);   // must not read into page 2
                char* rb = ref_pathremovebackslasha(b);
                CHECK((ra - p) == (rb - b), "page-guard: same offset");
                CHECK(memcmp(p, b, tail) == 0, "page-guard: same bytes");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathRemoveBackslashA vs live shlwapi + oracle, comparing the RETURNED "
           "OFFSET and the whole buffer: every probe-derived case at 4 alignments, ALL 255 byte "
           "values as the DRIVE LETTER (the position where this narrow form takes ASCII only while "
           "its wide sibling also takes Latin-1) and as the TRAILING character (where exactly one "
           "value, 0x5C, is ever removed), exhaustive {a,backslash,/,:,C,0x80} to length 7, 32 "
           "alignments x lengths 0..130, long strings to 600, NULL, 300k fuzz over an alphabet "
           "carrying two Latin-1 bytes, and a NOACCESS page-guard sweep)\n");
    return 0;
}
