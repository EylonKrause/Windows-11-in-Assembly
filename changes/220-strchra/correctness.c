// changes/220-strchra/correctness.c
// Gate 1: wia_strchra must be indistinguishable from shlwapi!StrChrA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// Two things drive the shape.
//
// 1. The first load is aligned DOWN and the leading bits are cleared rather than shifted out, so
//    every case runs at every start offset within a 32-byte block -- and with copies of the target
//    planted in front of the string, since that is what a mis-cleared first mask would find.
// 2. Every byte value has to be proved as the target, including 0x80..0xFF, which are ordinary
//    characters on code page 1252 and which a signed compare would get wrong.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern const char* wia_strchra(const char*, WORD);
const char* ref_strchra(const char*, WORD);
typedef char* (WINAPI *FN)(const char*, WORD);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

/* compared as offset-or-absent, which is what a caller can observe */
static int one(const char* s, unsigned m){
    const char* a = wia_strchra(s, (WORD)m);
    const char* b = ref_strchra(s, (WORD)m);
    char* c = sys(s, (WORD)m);
    long long x = a ? (a - s) : -1, y = b ? (b - s) : -1, z = c ? (c - s) : -1;
    return x == y && x == z;
}

static char pad[8192];
/* every start offset in a 32-byte block, with the TARGET planted in front of the string */
static int one_all_offsets(const char* s, int n, unsigned m){
    for (int off = 0; off < 32; ++off) {
        char* p = pad + 64 + off;
        for (int i = 1; i <= 64; ++i) p[-i] = (char)(m & 0xFF);
        memcpy(p, s, (size_t)n + 1);
        if (!one(p, m)) return 0;
    }
    return 1;
}

static unsigned long sd = 0x220220u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "StrChrA");
    if (!sys) { printf("CORRECTNESS: cannot resolve StrChrA\n"); return 1; }

    // ---- NULL, the terminator, the empty string ----
    CHECK(wia_strchra(NULL, 'a') == NULL, "NULL in, NULL out");
    CHECK(sys(NULL, 'a') == NULL, "the live export agrees NULL in, NULL out");
    CHECK(one("abc", 0),      "searching for the terminator returns NULL");
    CHECK(one("abc", 0x5A00), "a match value whose LOW byte is 0 returns NULL");
    CHECK(one("", 'a'),       "empty string");
    CHECK(one("", 0),         "empty string, searching for the terminator");

    // ---- only the LOW byte of wMatch counts ----
    {
        static const char s[] = "abcZdefZghi";
        CHECK(one(s, 'Z'),    "wMatch 0x005A");
        CHECK(one(s, 0x015A), "wMatch 0x015A, low byte 'Z'");
        CHECK(one(s, 0x5A5A), "wMatch 0x5A5A, low byte 'Z'");
        CHECK(one(s, 0xFF5A), "wMatch 0xFF5A, low byte 'Z'");
    }

    // ---- every byte value as the target, present and absent ----
    {
        for (int b = 1; b < 256; ++b) {
            char s[16];
            char f = (char)((b == 0xF1) ? 0xF2 : 0xF1);
            s[0]=f; s[1]=(char)b; s[2]=f; s[3]=(char)b; s[4]=0;
            CHECK(one(s, (unsigned)b), "every byte value, first of two occurrences");
            s[1]=f; s[3]=f;
            CHECK(one(s, (unsigned)b), "every byte value, absent");
            /* at the very last position, and as the very first character */
            s[3]=(char)b;
            CHECK(one(s, (unsigned)b), "every byte value, at the final position");
            s[3]=f; s[0]=(char)b;
            CHECK(one(s, (unsigned)b), "every byte value, at the first position");
        }
    }

    // ---- every length x every hit position, at every start offset -----------------------------
    {
        static char s[300];
        for (int len = 0; len <= 100; ++len) {
            for (int i = 0; i < len; ++i) s[i] = 'a';
            s[len] = 0;
            CHECK(one_all_offsets(s, len, 'Z'), "absent, every length, every start offset");
            for (int hit = 0; hit < len; ++hit) {
                s[hit] = 'Z';
                if (!one_all_offsets(s, len, 'Z')) {
                    CHECK(0, "hit at every position x every length x every start offset"); break;
                }
                s[hit] = 'a';
            }
        }
    }

    // ---- long strings through the multi-block loop ----
    {
        static char big[5000];
        for (int len = 100; len <= 4000; len += 311) {
            for (int off = 0; off < 33; off += 8) {
                char* s = big + off;
                for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
                s[len] = 0;
                CHECK(one(s, '#'), "long, absent");
                s[len-1] = '#';
                CHECK(one(s, '#'), "long, hit on the final character");
                s[len-1] = 'a'; s[0] = '#';
                CHECK(one(s, '#'), "long, hit on the first character");
                s[0] = 'a'; s[len/2] = '#';
                CHECK(one(s, '#'), "long, hit in the middle");
                s[len/2] = 'a';
            }
        }
    }

    // ---- fuzz over the full byte range ----
    {
        static char s[600];
        for (int t = 0; t < 300000; ++t) {
            int len = (int)(rnd() % 500);
            unsigned target = 1 + rnd() % 255;
            for (int i = 0; i < len; ++i) {
                unsigned k = rnd() % 10;
                s[i] = (k == 0) ? (char)target : (char)(1 + rnd() % 255);
            }
            s[len] = 0;
            CHECK(one(s, target), "fuzz, full byte range");
        }
    }

    // ---- GUARD PAGE: the terminator on the last byte of a mapped page ----
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
            CHECK(one(s, '#'), "guard page: absent, must stop at the terminator");
            s[len-1] = '#';
            CHECK(one(s, '#'), "guard page: hit on the final character");
            s[len-1] = 'a';
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StrChrA vs live shlwapi + oracle, compared as offsets: NULL; "
           "searching for the terminator, and a match value whose LOW byte is 0, both of which "
           "return NULL; the empty string; the WORD match value with four different high bytes, "
           "proving only the low one counts; EVERY byte value 0x01..0xFF as the target in four "
           "roles -- first of two occurrences, absent, final position and first position -- since "
           "0x80..0xFF are ordinary characters here and a signed compare would get them wrong; every "
           "length 0..100 x every hit position x EVERY start offset within a 32-byte block, WITH "
           "COPIES OF THE TARGET PLANTED IN FRONT OF THE STRING, because the first load is aligned "
           "DOWN and a mis-cleared leading mask would find one of them; long strings to 4000 "
           "characters through the multi-block loop at five alignments, with the hit first, last, "
           "middle and absent; 300k fuzz over the full byte range; and a GUARD PAGE sweep for every "
           "length 1..200 where the scan must stop at the terminator without touching the "
           "PAGE_NOACCESS page)\n");
    return 0;
}
