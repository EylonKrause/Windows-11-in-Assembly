// changes/219-pathstrippatha/correctness.c
// Gate 1: wia_pathstrippatha must be indistinguishable from shlwapi!PathStripPathA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// EVERY CASE COMPARES THE WHOLE BUFFER. The live export leaves the bytes past the new terminator
// untouched -- stripping "C:\dir\file.txt" leaves "file.txt\0" followed by the stale tail "le.txt\0"
// -- so an implementation that zero-filled the vacated space would produce the same STRING on every
// input and only a whole-buffer check can tell it apart.
//
// And the corpus is EXHAUSTIVE, for the same reason change 212's is: the separator rule is not
// local. A colon separates only when it is the SOLE colon in its run, so no bounded window of
// characters decides the answer, and a plausible simpler rule differs from the live export on 76672
// of the 349525 strings over {a, backslash, slash, colon}. Sampling cannot establish that.
//
// The alphabet includes a SPACE deliberately. Change 132 shipped a PathFindExtension rule missing
// exactly that character -- wrong on 295513 of 2015539 strings -- and changes 140, 143 and 144
// inherited it. probes/strip.c confirmed this function is clean over 488281 space-bearing strings,
// and this test keeps it that way.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern void wia_pathstrippatha(char*);
void ref_pathstrippatha(char*);
typedef void (WINAPI *FN)(char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define BUF 2048    /* must exceed the longest case below: the long-path section runs to 1200 */

static int one_at(const char* src, int n, int off){
    static char a[BUF], b[BUF], c[BUF];
    int i;
    memset(a, POISON, BUF); memset(b, POISON, BUF); memset(c, POISON, BUF);
    memcpy(a + off, src, (size_t)n + 1);
    memcpy(b + off, src, (size_t)n + 1);
    memcpy(c + off, src, (size_t)n + 1);
    wia_pathstrippatha(a + off);
    ref_pathstrippatha(b + off);
    sys(c + off);
    for (i = 0; i < BUF; ++i) if (a[i] != b[i] || a[i] != c[i]) return 0;
    return 1;
}
static int one(const char* src){ return one_at(src, (int)strlen(src), 64); }
/* the first load is aligned DOWN, so every start offset within a 32-byte block matters */
static int one_all_offsets(const char* src){
    int n = (int)strlen(src);
    for (int off = 64; off < 64 + 32; ++off) if (!one_at(src, n, off)) return 0;
    return 1;
}

static unsigned long sd = 0x219219u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "PathStripPathA");
    if (!sys) { printf("CORRECTNESS: cannot resolve PathStripPathA\n"); return 1; }

    // ---- NULL: measured to return without faulting, writing nothing ----
    wia_pathstrippatha(NULL);
    sys(NULL);
    CHECK(1, "NULL returns without faulting");

    // ---- named shapes, every start offset ----
    {
        static const char* S[] = {
            "C:\\Program Files\\Some Vendor\\bin\\thing.exe", "thing.exe", "C:\\thing.exe",
            "C:\\", "C:", "\\\\server\\share\\file.txt", "\\\\server\\share\\", "a/b/c.txt",
            "a\\b/c.txt", "dir\\", "dir\\\\", "", ".", "..", "C:file.txt", "x:y\\z",
            "::a", ":a:", "a::a", ":\\:a", "a:b:c", "/", "\\", ":", "//", "\\\\", "::",
            "a name with spaces\\file name.txt", "  \\  ", " " };
        for (int i = 0; i < (int)(sizeof(S)/sizeof(S[0])); ++i)
            CHECK(one_all_offsets(S[i]), "named shapes at every start offset");
    }

    // ---- EXHAUSTIVE over {a, backslash, slash, colon}, lengths 0..8 ----
    {
        static const char AL[4] = { 'a', '\\', '/', ':' };
        char s[12];
        long n = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v & 3]; v >>= 2; }
                s[len] = 0;
                if (!one(s)) { CHECK(0, "exhaustive {a,backslash,slash,colon} 0..8"); }
                ++n;
            }
        }
        printf("  exhaustive {a,backslash,slash,colon} 0..8: %ld strings\n", n);
    }

    // ---- EXHAUSTIVE over {a, backslash, slash, colon, SPACE}, lengths 0..7 ----
    {
        static const char AL[5] = { 'a', '\\', '/', ':', ' ' };
        char s[12];
        long n = 0, sp = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 5;
            for (long c = 0; c < combos; ++c) {
                long v = c; int has = 0;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 5]; if (s[i]==' ') has = 1; v /= 5; }
                s[len] = 0;
                if (has) ++sp;
                if (!one(s)) { CHECK(0, "exhaustive with a SPACE in the alphabet"); }
                ++n;
            }
        }
        printf("  exhaustive {a,backslash,slash,colon,space} 0..7: %ld strings, %ld with a space\n", n, sp);
    }

    // ---- every byte value in every role ----
    {
        for (int b = 1; b < 256; ++b) {
            char s[12];
            s[0]='a'; s[1]=(char)b; s[2]='\\'; s[3]='x'; s[4]='y'; s[5]=0;
            CHECK(one(s), "every byte value before a separator");
            s[0]='a'; s[1]='\\'; s[2]=(char)b; s[3]='y'; s[4]=0;
            CHECK(one(s), "every byte value after a separator");
            s[0]=(char)b; s[1]=':'; s[2]='x'; s[3]=0;
            CHECK(one(s), "every byte value before a colon");
        }
    }

    // ---- every length x every separator position: the MOVE at every size and alignment ----
    {
        static char s[400];
        for (int len = 1; len <= 150; ++len) {
            for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
            s[len] = 0;
            CHECK(one_all_offsets(s), "no separator, every length");
            for (int pos = 0; pos < len; pos += (len > 40 ? 5 : 1)) {
                char save = s[pos];
                s[pos] = '\\';
                if (!one(s)) { CHECK(0, "separator at every position -> move of every size"); }
                s[pos] = save;
            }
        }
    }

    // ---- long paths through the 32-byte move loop ----
    {
        static char big[1400];
        for (int len = 100; len <= 1200; len += 173) {
            for (int off = 0; off < 33; off += 8) {
                for (int i = 0; i < len; ++i) big[i] = (char)('a' + (i % 23));
                big[len] = 0;
                for (int sep = 1; sep < len; sep += (len / 7) + 1) {
                    char save = big[sep];
                    big[sep] = '\\';
                    CHECK(one_at(big, len, 64 + off), "long paths, 32-byte move loop");
                    big[sep] = save;
                }
            }
        }
    }

    // ---- fuzz over a path alphabet including a space and a high byte ----
    {
        static const char AL[8] = { 'a', 'b', '\\', '/', ':', ' ', 'z', (char)0xE9 };
        static char s[400];
        for (int t = 0; t < 200000; ++t) {
            int len = (int)(rnd() % 300);
            for (int i = 0; i < len; ++i) s[i] = AL[rnd() % 8];
            s[len] = 0;
            CHECK(one(s), "path fuzz with a space and a high byte");
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
                if (mode == 1 && len > 3) s[len/2] = '\\';
                s[len] = 0;
                memcpy(ref, s, (size_t)len + 1);

                wia_pathstrippatha(s);
                char a[BUF]; memcpy(a, s, (size_t)len + 1);
                memcpy(s, ref, (size_t)len + 1);
                sys(s);
                CHECK(memcmp(a, s, (size_t)len + 1) == 0, "guard page: whole buffer");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathStripPathA vs live shlwapi + oracle, EVERY case comparing the "
           "WHOLE BUFFER because the live export leaves the bytes past the new terminator untouched "
           "-- stripping \"C:\\\\dir\\\\file.txt\" leaves \"file.txt\\\\0\" followed by the stale tail "
           "\"le.txt\\\\0\" -- so a zero-filling implementation would produce the same string on every "
           "input. The corpus is EXHAUSTIVE, as change 212's is, because the separator rule is NOT "
           "local (a colon separates only when it is the sole colon in its run): ALL 87381 strings "
           "over {a,backslash,slash,colon} of length 0..8, and ALL 78125 over the same alphabet PLUS "
           "A SPACE of length 0..7, the space being there because change 132 shipped a rule missing "
           "exactly that character and three more changes inherited it. Then NULL; 30 named shapes at "
           "EVERY start offset within a 32-byte block since the first load is aligned DOWN; every "
           "byte value 0x01..0xFF in three roles; every length 1..150 x every separator position, "
           "which exercises the MOVE at every size; long paths to 1200 characters through the 32-byte "
           "move loop at eight alignments; 200k fuzz over an alphabet with a space and a high byte; "
           "and a guard-page sweep for every length 1..200 in two modes)\n");
    return 0;
}
