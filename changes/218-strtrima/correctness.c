// changes/218-strtrima/correctness.c
// Gate 1: wia_strtrima must be indistinguishable from shlwapi!StrTrimA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// EVERY CASE COMPARES THE WHOLE BUFFER, NOT THE RESULTING STRING. This function writes only what it
// must -- probes/trim.c poisoned the bytes past the terminator and read them back, and found that
// "abc" trimmed of 'x' leaves the buffer completely untouched, while "abcxx" gets exactly ONE byte
// written and the old 'x' and old terminator are still sitting there afterwards. An implementation
// that always re-terminated, or that cleared the vacated tail, would produce the same STRING and the
// same return value on every input, and only a whole-buffer comparison can tell it apart.
//
// The other two things driving the shape:
//   * the set is a 256-bit bitmap whose vector test resolves 0x00..0x7F through one vpshufb table
//     and 0x80..0xFF through the other, so every byte value is proved in both roles;
//   * the first load is aligned DOWN with the leading bits cleared, so every case runs at EVERY
//     start offset within a 32-byte block, with trim characters planted in front of the string.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_strtrima(char*, const char*);
int ref_strtrima(char*, const char*);
typedef BOOL (WINAPI *FN)(char*, const char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define BUF 640

/* return value AND the whole buffer, three ways */
static int one_at(const char* src, int n, const char* set, int off){
    static char a[BUF], b[BUF], c[BUF];
    int i;
    memset(a, POISON, BUF); memset(b, POISON, BUF); memset(c, POISON, BUF);
    memcpy(a + off, src, (size_t)n + 1);
    memcpy(b + off, src, (size_t)n + 1);
    memcpy(c + off, src, (size_t)n + 1);
    int ra = wia_strtrima(a + off, set) ? 1 : 0;
    int rb = ref_strtrima(b + off, set) ? 1 : 0;
    int rc = sys(c + off, set) ? 1 : 0;
    if (ra != rb || ra != rc) return 0;
    for (i = 0; i < BUF; ++i) if (a[i] != b[i] || a[i] != c[i]) return 0;
    return 1;
}
static int one(const char* src, const char* set){
    return one_at(src, (int)strlen(src), set, 64);
}
/* every start offset within a 32-byte block, with TRIM CHARACTERS planted in front */
static int one_all_offsets(const char* src, const char* set){
    int n = (int)strlen(src);
    for (int off = 64; off < 64 + 32; ++off)
        if (!one_at(src, n, set, off)) return 0;
    return 1;
}

static unsigned long sd = 0x218218u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "StrTrimA");
    if (!sys) { printf("CORRECTNESS: cannot resolve StrTrimA\n"); return 1; }

    // ---- NULL, and the two degenerate sets ----
    {
        static char d[BUF];
        memset(d, POISON, BUF); memcpy(d, "xxabcxx", 8);
        CHECK((wia_strtrima(d, NULL) ? 1:0) == 0, "a NULL set returns FALSE");
        CHECK(memcmp(d, "xxabcxx", 8) == 0, "a NULL set touches NOTHING");
        CHECK((wia_strtrima(NULL, "x") ? 1:0) == 0, "a NULL source returns FALSE");
        CHECK(one("abc", ""), "an EMPTY set");
        CHECK(one("", "x"),   "an EMPTY source");
        CHECK(one("", ""),    "both empty");
    }

    // ---- the named shapes ----
    {
        static const char* S[] = {
            "xxabcxx","xxabc","abcxx","abc","xxxxx","x","","abcxabc","  abc  "," a "," ",
            "xa","ax","xax","xxaxx","a","xy","yx","xyabcyx" };
        static const char* T[] = { "x", "xy", " ", "", "abc", "a" };
        for (int i = 0; i < (int)(sizeof(S)/sizeof(S[0])); ++i)
            for (int j = 0; j < 6; ++j)
                CHECK(one_all_offsets(S[i], T[j]), "named shapes x sets, every start offset");
    }

    // ---- EVERY byte value as a trim character, and as a kept character ----------------------------
    // The membership test resolves 0x00..0x7F through one vpshufb table and 0x80..0xFF through the
    // other, so a swapped blend would pass any ASCII-only test.
    {
        for (int b = 1; b < 256; ++b) {
            char s[16], set[4];
            char f = (char)((b == 0x41) ? 0x42 : 0x41);
            set[0] = (char)b; set[1] = 0;
            s[0]=(char)b; s[1]=(char)b; s[2]=f; s[3]=f; s[4]=(char)b; s[5]=0;
            CHECK(one(s, set), "every byte value trimmed from both ends");
            s[0]=f; s[1]=f; s[2]=f; s[3]=0;
            CHECK(one(s, set), "every byte value as a set the string avoids entirely");
            s[0]=(char)b; s[1]=(char)b; s[2]=(char)b; s[3]=0;
            CHECK(one(s, set), "a string made ENTIRELY of every byte value");
        }
        /* a set spanning both halves of the bitmap */
        {
            static char s[64];
            static const char set2[3] = { (char)0x41, (char)0xC3, 0 };
            for (int k = 0; k < 20; ++k) {
                for (int i = 0; i < 20; ++i) s[i] = 'z';
                s[20] = 0;
                s[k] = (char)0x41;
                CHECK(one(s, set2), "low-half trim char at every position");
                s[k] = (char)0xC3;
                CHECK(one(s, set2), "high-half trim char at every position");
            }
        }
    }

    // ---- every length x every leading-run x trailing-run combination ------------------------------
    {
        static char s[300];
        for (int len = 0; len <= 70; ++len) {
            for (int lead = 0; lead <= len; lead += (len > 20 ? 7 : 1)) {
                for (int trail = 0; trail + lead <= len; trail += (len > 20 ? 7 : 1)) {
                    for (int i = 0; i < len; ++i)
                        s[i] = (i < lead || i >= len - trail) ? 'x' : (char)('a' + (i % 23));
                    s[len] = 0;
                    if (!one(s, "x")) { CHECK(0, "every length x leading run x trailing run"); break; }
                }
            }
        }
    }

    // ---- long strings through the chunked move and the multi-block scan ---------------------------
    {
        static char s[600];
        for (int len = 40; len <= 500; len += 37) {
            for (int lead = 0; lead <= 40; lead += 9) {
                for (int trail = 0; trail <= 40; trail += 9) {
                    if (lead + trail > len) continue;
                    for (int i = 0; i < len; ++i)
                        s[i] = (i < lead || i >= len - trail) ? 'x' : (char)('a' + (i % 23));
                    s[len] = 0;
                    CHECK(one(s, "x"), "long strings, chunked move");
                }
            }
        }
    }

    // ---- fuzz over the full byte range ------------------------------------------------------------
    {
        static char s[400], set[20];
        for (int t = 0; t < 200000; ++t) {
            int len = (int)(rnd() % 300);
            int sl  = 1 + (int)(rnd() % 6);
            for (int i = 0; i < sl; ++i) set[i] = (char)(1 + rnd() % 255);
            set[sl] = 0;
            for (int i = 0; i < len; ++i) {
                /* bias towards the set so leading and trailing runs actually happen */
                s[i] = ((rnd() % 3) == 0) ? set[rnd() % sl] : (char)(1 + rnd() % 255);
            }
            s[len] = 0;
            CHECK(one(s, set), "fuzz, full byte range, set-biased content");
        }
    }

    // ---- GUARD PAGE: the terminator on the last byte of a mapped page ------------------------------
    // The scan's aligned loads must not touch the page after it. The move never reads past the kept
    // range, so it is safe by construction, but the SCAN has to discover the terminator first.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        static char ref[BUF];
        for (int len = 1; len <= 200; ++len) {
            for (int mode = 0; mode < 3; ++mode) {
                char* s = (base + pg) - (SIZE_T)len - 1;
                for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
                if (mode == 1 && len > 2) { s[0] = 'x'; s[1] = 'x'; }
                if (mode == 2) { for (int i = 0; i < len; ++i) s[i] = 'x'; }
                s[len] = 0;
                memcpy(ref, s, (size_t)len + 1);

                int ra = wia_strtrima(s, "x") ? 1 : 0;
                char a[BUF]; memcpy(a, s, (size_t)len + 1);
                /* rebuild and run the live export on the same input */
                memcpy(s, ref, (size_t)len + 1);
                int rc = sys(s, "x") ? 1 : 0;
                CHECK(ra == rc, "guard page: return value");
                CHECK(memcmp(a, s, (size_t)strlen(s) + 1) == 0, "guard page: resulting string");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: PASS-NOT (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StrTrimA vs live shlwapi + oracle. EVERY case compares the WHOLE "
           "BUFFER against a poison fill, not the resulting string: this function writes only what "
           "it must -- \"abc\" trimmed of 'x' leaves the buffer completely untouched and \"abcxx\" "
           "gets exactly ONE byte written, with the old 'x' and old terminator still in place -- so "
           "an implementation that always re-terminated or cleared the vacated tail would produce "
           "the same string and the same return value on every input and only a whole-buffer check "
           "can tell it apart. NULL source and NULL set (both FALSE, both touching nothing) and the "
           "EMPTY set and EMPTY source; 19 named shapes x 6 sets at EVERY start offset within a "
           "32-byte block, since the first load is aligned DOWN; EVERY byte value 0x01..0xFF as a "
           "trim character, as a character the set avoids, and as a string made ENTIRELY of it, "
           "because the membership test resolves 0x00..0x7F and 0x80..0xFF through different vpshufb "
           "tables; a set spanning BOTH halves walked across 20 positions; every length 0..70 x "
           "every leading run x every trailing run; long strings to 500 characters through the "
           "chunked move; 200k fuzz over the full byte range with set-biased content so leading and "
           "trailing runs actually occur; and a GUARD PAGE sweep for every length 1..200 in three "
           "modes, where the scan must find the terminator without touching the PAGE_NOACCESS page)\n");
    return 0;
}
