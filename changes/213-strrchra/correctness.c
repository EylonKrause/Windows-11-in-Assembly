// changes/213-strrchra/correctness.c
// Gate 1: wia_strrchra must be indistinguishable from shlwapi!StrRChrA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// THE DOMAIN IS PART OF THE TEST. probes/srca.c established that the shipped export walks FORWARD
// with CharNextA, which does not advance past a terminator, so an pszEnd placed BEYOND the string's
// NUL makes it spin forever -- measured twice, once at the cost of a 300-second timeout. Every
// bounded case below therefore keeps pszEnd inside [pszStart, pszStart+strlen]. That is not the test
// being lenient: outside that range the shipped function produces no result at all, so there is
// nothing to be indistinguishable FROM.
//
// Within the domain the test is aggressive:
//   * every start offset within a 32-byte block, because all loads are 32-byte ALIGNED and the
//     leading/trailing bytes are masked out of the compare result rather than by narrowing the load;
//   * every end offset, because the bounded path masks bits at BOTH ends of the block;
//   * every byte value as the target, including 0x80..0xFF, which are ordinary characters here;
//   * the guard page, where an aligned load must not touch the page after the terminator.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern const char* wia_strrchra(const char*, const char*, WORD);
const char* ref_strrchra(const char*, const char*, WORD);
typedef char* (WINAPI *FN)(const char*, const char*, WORD);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

/* compare as offsets-or-absent, which is what a caller can observe */
static int one(const char* s, const char* e, unsigned m){
    const char* a = wia_strrchra(s, e, (WORD)m);
    const char* b = ref_strrchra(s, e, (WORD)m);
    char* c = sys(s, e, (WORD)m);
    long long x = a ? (a - s) : -1, y = b ? (b - s) : -1, z = c ? (c - s) : -1;
    return x == y && x == z;
}

static unsigned long sd = 0x213213u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

static char pad[8192];

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "StrRChrA");
    if (!sys) { printf("CORRECTNESS: cannot resolve StrRChrA\n"); return 1; }

    // ---- NULL start, and searching for the terminator ----
    CHECK(wia_strrchra(NULL, NULL, 'Z') == NULL, "NULL start -> NULL");
    CHECK(sys(NULL, NULL, 'Z') == NULL, "the live export agrees NULL start -> NULL");
    {
        static const char s[] = "abc";
        CHECK(one(s, NULL, 0),      "wMatch low byte 0 -> NULL");
        CHECK(one(s, s + 3, 0),     "wMatch low byte 0, explicit end -> NULL");
        CHECK(one(s, NULL, 0x5A00), "wMatch 0x5A00 (low byte NUL) -> NULL");
    }

    // ---- only the LOW BYTE of wMatch is consulted ----
    {
        static const char s[] = "abcZdefZghi";
        CHECK(one(s, NULL, 'Z'),    "wMatch 0x005A");
        CHECK(one(s, NULL, 0x015A), "wMatch 0x015A, low byte 'Z'");
        CHECK(one(s, NULL, 0x5A5A), "wMatch 0x5A5A, low byte 'Z'");
        CHECK(one(s, NULL, 0xFF5A), "wMatch 0xFF5A, low byte 'Z'");
    }

    // ---- EVERY start offset x EVERY end offset, on a string with matches all over it -------------
    // The bounded path masks bits at both ends of a 32-byte block, and the unbounded path shifts the
    // first block's mask by the start's low five bits, so both ends need every alignment.
    {
        for (int off = 0; off < 40; ++off) {
            char* s = pad + 128 + off;
            for (int len = 0; len <= 80; ++len) {
                for (int i = 0; i < len; ++i)
                    s[i] = (char)((i % 7 == 3) ? 'Z' : ('a' + (i % 23)));
                s[len] = 0;
                CHECK(one(s, NULL, 'Z'), "every start offset, unbounded");
                CHECK(one(s, NULL, 'q'), "every start offset, unbounded, absent");
                /* pszEnd anywhere inside the DOMAIN: [s, s+len] */
                for (int e = 0; e <= len; ++e) {
                    if (!one(s, s + e, 'Z')) { CHECK(0, "every start x every end, bounded"); break; }
                }
            }
        }
    }

    // ---- every byte value as the target, including the high half --------------------------------
    {
        for (int b = 1; b < 256; ++b) {
            char t[16];
            /* filler that cannot collide with the target, so the expected answer is unambiguous */
            char f = (char)((b == 0xF1) ? 0xF2 : 0xF1);
            t[0]=f; t[1]=(char)b; t[2]=f; t[3]=(char)b; t[4]=f; t[5]=0;
            CHECK(one(t, NULL, (WORD)b), "every byte value as target, unbounded");
            CHECK(one(t, t + 5, (WORD)b), "every byte value as target, bounded to the whole string");
            CHECK(one(t, t + 3, (WORD)b), "every byte value as target, bounded before the last one");
            CHECK(one(t, t + 1, (WORD)b), "every byte value as target, bounded to one character");
        }
    }

    // ---- long strings through the multi-block paths ----------------------------------------------
    {
        static char big[4096];
        for (int len = 100; len <= 3000; len += 331) {
            for (int off = 0; off < 33; off += 8) {
                char* s = big + off;
                for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
                s[len] = 0;
                CHECK(one(s, NULL, 'Z'), "long, target absent, unbounded");
                CHECK(one(s, s + len, 'Z'), "long, target absent, bounded");
                s[0] = 'Z';
                CHECK(one(s, NULL, 'Z'), "long, single match at the very start");
                s[0] = 'a'; s[len-1] = 'Z';
                CHECK(one(s, NULL, 'Z'), "long, single match at the very end");
                CHECK(one(s, s + len - 1, 'Z'), "long, match excluded by an end one short of it");
                s[len/2] = 'Z';
                CHECK(one(s, NULL, 'Z'), "long, two matches");
                CHECK(one(s, s + len/2, 'Z'), "long, end sitting exactly on a match");
                CHECK(one(s, s + len/2 + 1, 'Z'), "long, end one past a match");
                s[len-1] = 'a'; s[len/2] = 'a';
            }
        }
    }

    // ---- fuzz, inside the domain -----------------------------------------------------------------
    {
        static char s[600];
        for (int t = 0; t < 300000; ++t) {
            int len = (int)(rnd() % 500);
            unsigned target = 1 + rnd() % 255;
            for (int i = 0; i < len; ++i) {
                unsigned k = rnd() % 8;
                s[i] = (k == 0) ? (char)target : (char)(1 + rnd() % 255);
                if (s[i] == 0) s[i] = 'x';
            }
            s[len] = 0;
            int e = (int)(rnd() % (len + 1));
            CHECK(one(s, NULL, target),  "fuzz, unbounded");
            CHECK(one(s, s + e, target), "fuzz, bounded inside the domain");
        }
    }

    // ---- GUARD PAGE: the terminator on the last byte of a mapped page ----------------------------
    // The aligned 32-byte loads must never touch the page after it -- in the unbounded path, which
    // is the one that has to discover the terminator for itself.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        for (int len = 1; len <= 200; ++len) {
            char* s = (base + pg) - (SIZE_T)len - 1;       /* s[len] is the last mapped byte */
            for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
            s[len] = 0;
            CHECK(one(s, NULL, 'Z'), "guard page: absent, unbounded");
            CHECK(one(s, s + len, 'Z'), "guard page: absent, bounded to the terminator");
            s[len-1] = 'Z';
            CHECK(one(s, NULL, 'Z'), "guard page: match on the final character");
            CHECK(one(s, s + len, 'Z'), "guard page: match on the final character, bounded");
            s[len-1] = 'a';
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StrRChrA vs live shlwapi + oracle -- compared as offsets. Every case "
           "stays inside the CONTRACT DOMAIN (pszEnd NULL, or pszStart <= pszEnd <= pszStart+strlen) "
           "because probes/srca.c established that the shipped export walks forward with CharNextA, "
           "which does not advance past a terminator, so an pszEnd beyond the NUL makes it spin "
           "forever -- measured twice, once at the cost of a 300-second timeout -- and there is "
           "nothing to be indistinguishable from where it never returns. Inside the domain: NULL "
           "start; a low match byte of 0 which can never occur in a valid range; the WORD match "
           "value with four different high bytes, proving only the low one counts; EVERY start "
           "offset 0..39 x EVERY source length 0..80 x EVERY end offset 0..len, since the bounded "
           "path masks bits at BOTH ends of a 32-byte block and the unbounded path shifts the first "
           "block's mask by the start's low five bits; every byte value 0x01..0xFF as the target "
           "(0x80..0xFF are ordinary characters on code page 1252) with four different bounds each; "
           "long strings to 3000 characters through the multi-block paths, including an end sitting "
           "EXACTLY on a match and one character short of it; 300k fuzz; and a GUARD PAGE section "
           "placing the terminator on the last mapped byte for every length 1..200, where the "
           "unbounded path must discover the terminator without touching the PAGE_NOACCESS page)\n");
    return 0;
}
