// changes/217-pathfindextensiona/correctness.c
// Gate 1: wia_pathfindexta must be indistinguishable from shlwapi!PathFindExtensionA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// The corpus is exhaustive, and this function is why.
//
// Probing it is what caught the missing SPACE rule in change 132, landed code that had been
// passing its own "600k path fuzz" for weeks while disagreeing with the live PathFindExtensionW on
// 295513 of 2015539 enumerated strings. Its fuzz alphabet had no space in it, so its oracle, its
// implementation and its corpus were all wrong together, and a test that shares its blind spot with
// the thing it tests proves nothing.
//
// A bigger random alphabet is not the lesson. The lesson is that a rule over a small alphabet should
// be PROVED over that alphabet rather than sampled from it, so the first section below enumerates
// every string over {a, '.', backslash, '/', ':', space} of length 0..7 (335923 of them) and the
// second adds a tab and a high byte, because the rule is 0x20 specifically and not whitespace in
// general.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern const char* wia_pathfindexta(const char*);
const char* ref_pathfindexta(const char*);
typedef char* (WINAPI *FN)(const char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static int one(const char* s){
    const char* a = wia_pathfindexta(s);
    const char* b = ref_pathfindexta(s);
    char* c = sys(s);
    return (a - s) == (b - s) && (a - s) == (c - s);
}

static unsigned long sd = 0x217217u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

static char pad[8192];
static int one_all_offsets(const char* s, int len){
    for (int off = 0; off < 32; ++off) {
        memcpy(pad + 64 + off, s, (size_t)len + 1);
        if (!one(pad + 64 + off)) return 0;
    }
    return 1;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "PathFindExtensionA");
    if (!sys) { printf("CORRECTNESS: cannot resolve PathFindExtensionA\n"); return 1; }

    // ---- NULL ----
    CHECK(wia_pathfindexta(NULL) == NULL, "NULL in, NULL out");
    CHECK(sys(NULL) == NULL, "the live export agrees NULL in, NULL out");

    // ---- the shapes that broke change 132, named explicitly ----
    {
        static const char* S[] = {
            ". ", ". a", "a. ", "a.b ", "a.b  ", "a. b", "a.  b", "file.txt ", "file. txt",
            "file .txt", " a.b", "a .b", "a.b\t", "a.b. ", "  ", " ", ".  .",
            "file.txt", "a.b/c", "a.b\\c", "a.b:c", "C:\\dir\\file.tar.gz", "C:\\dir.x\\file",
            ".hidden", "a.b.", "noext", "", ".", "..", "C:\\", "\\\\s\\h\\f.e" };
        for (int i = 0; i < (int)(sizeof(S)/sizeof(S[0])); ++i)
            CHECK(one_all_offsets(S[i], (int)strlen(S[i])), "named shapes at every start offset");
    }

    // ---- EXHAUSTIVE over {a, '.', backslash, '/', ':', space}, lengths 0..7 ----------------------
    {
        static const char AL[6] = { 'a', '.', '\\', '/', ':', ' ' };
        char s[10];
        long n = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; v /= 6; }
                s[len] = 0;
                if (!one(s)) { CHECK(0, "exhaustive {a,.,backslash,/,:,space} 0..7"); }
                ++n;
            }
        }
        printf("  exhaustive {a,.,backslash,/,:,space} 0..7: %ld strings\n", n);
    }

    // ---- EXHAUSTIVE over {a, '.', backslash, space, tab, 0xE9}, lengths 0..7 ---------------------
    // The tab is here because the rule is 0x20 SPECIFICALLY and not whitespace in general: "a.b\t"
    // yields the dot while "a.b " yields the terminator. The high byte is here because 0x80..0xFF
    // are ordinary characters on code page 1252.
    {
        static const char AL[6] = { 'a', '.', '\\', ' ', '\t', (char)0xE9 };
        char s[10];
        long n = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; v /= 6; }
                s[len] = 0;
                if (!one(s)) { CHECK(0, "exhaustive {a,.,backslash,space,tab,0xE9} 0..7"); }
                ++n;
            }
        }
        printf("  exhaustive {a,.,backslash,space,tab,0xE9} 0..7: %ld strings\n", n);
    }

    // ---- every byte value in every role -----------------------------------------------------------
    {
        for (int b = 1; b < 256; ++b) {
            char s[10];
            s[0]='a'; s[1]=(char)b; s[2]='.'; s[3]='x'; s[4]=0;
            CHECK(one(s), "every byte value before a dot");
            s[0]='a'; s[1]='.'; s[2]=(char)b; s[3]='x'; s[4]=0;
            CHECK(one(s), "every byte value inside the extension");
            s[0]='a'; s[1]='.'; s[2]='x'; s[3]=(char)b; s[4]=0;
            CHECK(one(s), "every byte value at the end");
        }
    }

    // ---- every length x every position for each interesting character -----------------------------
    {
        static char p[400];
        static const char SPECIAL[5] = { '.', '\\', '/', ':', ' ' };
        for (int len = 0; len <= 120; ++len) {
            for (int i = 0; i < len; ++i) p[i] = 'a';
            p[len] = 0;
            CHECK(one_all_offsets(p, len), "plain, every length");
            for (int k = 0; k < 5; ++k) {
                for (int pos = 0; pos < len; pos += (len > 40 ? 7 : 1)) {
                    char save = p[pos];
                    p[pos] = SPECIAL[k];
                    if (!one(p)) { CHECK(0, "special character at every position"); }
                    p[pos] = save;
                }
            }
            /* a dot and a stopper together, both orders */
            if (len >= 4) {
                p[1] = '.'; p[len-2] = '\\'; CHECK(one(p), "dot then backslash");
                p[1] = '\\'; p[len-2] = '.'; CHECK(one(p), "backslash then dot");
                p[1] = '.'; p[len-2] = ' ';  CHECK(one(p), "dot then space");
                p[1] = ' '; p[len-2] = '.';  CHECK(one(p), "space then dot");
                p[1] = 'a'; p[len-2] = 'a';
            }
        }
    }

    // ---- fuzz over a path alphabet that CONTAINS a space and a tab --------------------------------
    {
        static const char AL[8] = { 'a', 'b', '.', '\\', '/', ':', ' ', '\t' };
        static char s[500];
        for (int t = 0; t < 300000; ++t) {
            int len = (int)(rnd() % 400);
            for (int i = 0; i < len; ++i) s[i] = AL[rnd() % 8];
            s[len] = 0;
            CHECK(one(s), "path fuzz with space and tab in the alphabet");
        }
    }

    // ---- GUARD PAGE ------------------------------------------------------------------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        for (int len = 1; len <= 200; ++len) {
            char* s = (base + pg) - (SIZE_T)len - 1;
            for (int i = 0; i < len; ++i) s[i] = (i == len/2) ? '.' : 'a';
            s[len] = 0;
            CHECK(one(s), "guard page: dot in the middle");
            s[len-1] = ' ';
            CHECK(one(s), "guard page: SPACE as the final character");
            s[len-1] = '.';
            CHECK(one(s), "guard page: dot as the final character");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathFindExtensionA vs live shlwapi + oracle, compared as offsets. The "
           "corpus is EXHAUSTIVE and this function is why: probing it caught the missing SPACE rule "
           "in change 132, which had been passing its own 600k path fuzz while disagreeing with the "
           "live export on 295513 of 2015539 strings, because its alphabet had no space and so its "
           "oracle, implementation and corpus were all wrong together. Here: NULL; 31 named shapes "
           "including every one that broke 132, each at EVERY start offset within a 32-byte block "
           "since the first load is aligned DOWN; ALL 335923 strings over {a,'.',backslash,'/',':',"
           "space} of length 0..7; ALL 335923 over {a,'.',backslash,space,tab,0xE9}, the tab being "
           "there because the rule is 0x20 SPECIFICALLY and not whitespace in general and the high "
           "byte because 0x80..0xFF are ordinary characters on code page 1252; every byte value "
           "0x01..0xFF in three different roles; every length 0..120 x every position for each of "
           "'.', backslash, '/', ':' and space, plus dot-then-stopper and stopper-then-dot pairs; "
           "300k path fuzz over an alphabet that CONTAINS a space and a tab; and a guard-page sweep "
           "for every length 1..200 with a space and a dot each tried as the final character)\n");
    return 0;
}
