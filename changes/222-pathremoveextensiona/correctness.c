// changes/222-pathremoveextensiona/correctness.c
// Gate 1: wia_pathremoveexta must be indistinguishable from shlwapi!PathRemoveExtensionA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// Every case compares the whole buffer. The export writes exactly one byte -- the terminator at the
// extension position -- and clears nothing past it: "file.txt" becomes "file" with "txt" and the
// original terminator still in the buffer. An implementation that zero-filled the removed extension
// would leave the same STRING on every input, and this function returns nothing at all.
//
// The corpus is exhaustive over an alphabet that contains a space, and that is not decoration. This
// change's own rule was wrong in three landed siblings until earlier today: change 132 shipped a
// PathFindExtension rule with only the backslash stopping the backward scan, a SPACE stops it too,
// and changes 140, 143 and 144 inherited the omission. probes/rmext.c measured the narrow REMOVE
// against both rules over 335923 strings -- 0 mismatches against the corrected one, 46158 against
// the one 140 shipped with -- and this test keeps it that way.
//
// And the MAX_PATH guard is tested at every length across the boundary, because it is the one rule
// this function has that its find-only sibling does not.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern void wia_pathremoveexta(char*);
void ref_pathremoveexta(char*);
typedef void (WINAPI *FN)(char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define BUF 1024

static int one_at(const char* src, int n, int off){
    static char a[BUF], b[BUF], c[BUF];
    int i;
    memset(a, POISON, BUF); memset(b, POISON, BUF); memset(c, POISON, BUF);
    memcpy(a + off, src, (size_t)n + 1);
    memcpy(b + off, src, (size_t)n + 1);
    memcpy(c + off, src, (size_t)n + 1);
    wia_pathremoveexta(a + off);
    ref_pathremoveexta(b + off);
    sys(c + off);
    for (i = 0; i < BUF; ++i) if (a[i] != b[i] || a[i] != c[i]) return 0;
    return 1;
}
static int one(const char* src){ return one_at(src, (int)strlen(src), 64); }
static int one_all_offsets(const char* src){
    int n = (int)strlen(src);
    for (int off = 64; off < 64 + 32; ++off) if (!one_at(src, n, off)) return 0;
    return 1;
}

static unsigned long sd = 0x222222u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "PathRemoveExtensionA");
    if (!sys) { printf("CORRECTNESS: cannot resolve PathRemoveExtensionA\n"); return 1; }

    // ---- NULL ----
    wia_pathremoveexta(NULL);
    sys(NULL);
    CHECK(1, "NULL returns without faulting");

    // ---- named shapes, every start offset ----
    {
        static const char* S[] = {
            "file.txt", "file", "a.b.c", "dir\\file.txt", "dir.x\\file", "a.b ", "a .b",
            ".hidden", "a.b.", "", ".", "..", "a.b/c", "a.b:c", "C:\\dir\\file.tar.gz",
            "a.b\t", "file. txt", "file .txt", " ", "  ", ".  .", "a. ", ". a" };
        for (int i = 0; i < (int)(sizeof(S)/sizeof(S[0])); ++i)
            CHECK(one_all_offsets(S[i]), "named shapes at every start offset");
    }

    // ---- EXHAUSTIVE over {a, '.', backslash, '/', ':', SPACE}, lengths 0..7 ----
    {
        static const char AL[6] = { 'a', '.', '\\', '/', ':', ' ' };
        char s[12];
        long n = 0, sp = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c; int has = 0;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; if (s[i]==' ') has = 1; v /= 6; }
                s[len] = 0;
                if (has) ++sp;
                if (!one(s)) { CHECK(0, "exhaustive with a SPACE in the alphabet"); }
                ++n;
            }
        }
        printf("  exhaustive {a,.,backslash,/,:,space} 0..7: %ld strings, %ld with a space\n", n, sp);
    }

    // ---- EXHAUSTIVE over {a, '.', backslash, space, tab, 0xE9}, lengths 0..7 ----
    // The tab is here because the rule is 0x20 SPECIFICALLY and not whitespace in general.
    {
        static const char AL[6] = { 'a', '.', '\\', ' ', '\t', (char)0xE9 };
        char s[12];
        long n = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; v /= 6; }
                s[len] = 0;
                if (!one(s)) { CHECK(0, "exhaustive with a tab and a high byte"); }
                ++n;
            }
        }
        printf("  exhaustive {a,.,backslash,space,tab,0xE9} 0..7: %ld strings\n", n);
    }

    // ---- THE MAX_PATH GUARD, at every length across the boundary ---------------------------------
    // The one rule this function has that PathFindExtensionA does not: 259 truncates, 260 does not.
    {
        static char big[600];
        for (int len = 200; len <= 320; ++len) {
            for (int dotpos = 1; dotpos < len; dotpos += (len / 5) + 1) {
                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                big[dotpos] = '.';
                big[len] = 0;
                if (!one_at(big, len, 64)) { CHECK(0, "the MAX_PATH guard at every length"); break; }
            }
            /* and with no extension at all */
            for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
            big[len] = 0;
            CHECK(one_at(big, len, 64), "every length, no extension");
        }
    }

    // ---- every byte value in every role ----
    {
        for (int b = 1; b < 256; ++b) {
            char s[12];
            s[0]='a'; s[1]=(char)b; s[2]='.'; s[3]='x'; s[4]=0;
            CHECK(one(s), "every byte value before the dot");
            s[0]='a'; s[1]='.'; s[2]=(char)b; s[3]='x'; s[4]=0;
            CHECK(one(s), "every byte value inside the extension");
            s[0]='a'; s[1]='.'; s[2]='x'; s[3]=(char)b; s[4]=0;
            CHECK(one(s), "every byte value at the end");
        }
    }

    // ---- every length x every dot position ----
    {
        static char s[400];
        for (int len = 1; len <= 150; ++len) {
            for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
            s[len] = 0;
            CHECK(one_all_offsets(s), "no dot, every length");
            for (int pos = 0; pos < len; pos += (len > 40 ? 7 : 1)) {
                char save = s[pos];
                s[pos] = '.';
                if (!one(s)) { CHECK(0, "a dot at every position x every length"); break; }
                s[pos] = save;
            }
        }
    }

    // ---- fuzz over a path alphabet containing a space and a tab ----
    {
        static const char AL[8] = { 'a', 'b', '.', '\\', '/', ':', ' ', '\t' };
        static char s[400];
        for (int t = 0; t < 200000; ++t) {
            int len = (int)(rnd() % 320);          /* straddles the MAX_PATH boundary */
            for (int i = 0; i < len; ++i) s[i] = AL[rnd() % 8];
            s[len] = 0;
            CHECK(one(s), "path fuzz across the MAX_PATH boundary");
        }
    }

    // ---- GUARD PAGE ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        static char ref[BUF];
        for (int len = 1; len <= 200; ++len) {
            for (int mode = 0; mode < 2; ++mode) {
                char* s = (base + pg) - (SIZE_T)len - 1;
                for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
                if (mode == 1 && len > 3) s[len/2] = '.';
                s[len] = 0;
                memcpy(ref, s, (size_t)len + 1);
                wia_pathremoveexta(s);
                char a[BUF]; memcpy(a, s, (size_t)len + 1);
                memcpy(s, ref, (size_t)len + 1);
                sys(s);
                CHECK(memcmp(a, s, (size_t)len + 1) == 0, "guard page: whole buffer");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathRemoveExtensionA vs live shlwapi + oracle, EVERY case comparing "
           "the WHOLE BUFFER because the export writes exactly ONE byte and clears nothing past it "
           "-- \"file.txt\" becomes \"file\" with \"txt\" still in the buffer -- so a zero-filling "
           "implementation would leave the same string on every input, and this function returns "
           "nothing at all. NULL; 23 named shapes at EVERY start offset within a 32-byte block; ALL "
           "335923 strings over {a,'.',backslash,'/',':',SPACE} of length 0..7, the space being "
           "there because this change's own rule was wrong in three landed siblings until today and "
           "the narrow REMOVE disagrees with the old rule on 46158 of them; ALL 335923 over "
           "{a,'.',backslash,space,tab,0xE9}, the tab because the rule is 0x20 specifically and not "
           "whitespace; THE MAX_PATH GUARD at every length 200..320 x several dot positions, which "
           "is the one rule this function has that PathFindExtensionA does not -- 259 truncates, 260 "
           "is left completely untouched; every byte value 0x01..0xFF in three roles; every length "
           "1..150 x every dot position; 200k path fuzz straddling the MAX_PATH boundary; and a "
           "guard-page sweep for every length 1..200 in two modes)\n");
    return 0;
}
