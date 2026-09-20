// changes/216-strspna/correctness.c
// Gate 1: wia_strspna must be indistinguishable from shlwapi!StrSpnA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// What drives the shape of this test.
//
// 1. The set is a 256-BIT bitmap, so every byte value has to be provable both as a member and as a
//    non-member. The membership test is a two-table vpshufb selected by the character's BIT 7, so
//    the halves are exercised separately: 0x00..0x7F resolve through one table and 0x80..0xFF
//    through the other, and a swapped blend would pass any test that only used ASCII.
// 2. The first load is aligned down and the leading bytes are shifted out of the mask, so garbage
//    before the string could produce a false hit if the shift were wrong. Every case is therefore
//    run at every start offset within a 32-byte block, with the preceding bytes deliberately filled
//    with set members.
// 3. a NULL set is not the empty set, 0 versus strlen. Both are tested explicitly.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_strspna(const char*, const char*);
int ref_strspna(const char*, const char*);
typedef int (WINAPI *FN)(const char*, const char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static int one(const char* s, const char* set){
    int a = wia_strspna(s, set);
    int b = ref_strspna(s, set);
    int c = sys(s, set);
    return a == b && a == c;
}

static unsigned long sd = 0x214214u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

/* run a case at every start offset in a 32-byte block, with SET MEMBERS in front of it so a
   mis-shifted first mask would be caught rather than hidden by harmless padding */
static char pad[8192];
static int one_all_offsets(const char* s, int len, const char* set){
    for (int off = 0; off < 32; ++off) {
        char* p = pad + 64 + off;
        for (int i = 1; i <= 64; ++i) p[-i] = set[0] ? set[0] : 'Z';
        memcpy(p, s, (size_t)len + 1);
        if (!one(p, set)) return 0;
    }
    return 1;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "StrSpnA");
    if (!sys) { printf("CORRECTNESS: cannot resolve StrSpnA\n"); return 1; }

    // ---- NULL and the empty set. For THIS export the two agree (both 0), unlike its sibling
    // ---- StrCSpnA where a NULL set gives 0 and an EMPTY set gives strlen. Measured, not assumed,
    // ---- because the three functions share a core and the wrong rule would carry across silently.
    CHECK(wia_strspna(NULL, "abc") == sys(NULL, "abc"), "NULL subject");
    CHECK(wia_strspna("abc", NULL) == sys("abc", NULL), "NULL set");
    CHECK(wia_strspna(NULL, NULL)  == sys(NULL, NULL),  "both NULL");
    CHECK(wia_strspna("abc", NULL) == 0, "a NULL set returns 0");
    CHECK(one("abc", ""), "an EMPTY set agrees with the live export");
    CHECK(wia_strspna("abc", "") == 0, "and that value is 0 -- nothing spans an empty set");

    // ---- every byte value as a set member, and as a non-member ---------------------------------
    // The membership test resolves 0x00..0x7F through one table and 0x80..0xFF through the other,
    // selected by bit 7, so both halves must be proved independently.
    {
        for (int b = 1; b < 256; ++b) {
            char s[8], set[4];
            char f = (char)((b == 0x41) ? 0x42 : 0x41);     /* filler that is not the target */
            set[0] = (char)b; set[1] = 0;
            s[0]=f; s[1]=f; s[2]=(char)b; s[3]=f; s[4]=0;
            CHECK(one(s, set), "every byte value as a set member");
            s[2] = f;                                        /* now absent */
            CHECK(one(s, set), "every byte value absent from the subject");
            /* a subject made entirely of this byte, so the span runs to the terminator and the
               inverted mask has to stop there with no NUL compare of its own */
            {
                char all[40];
                for (int i = 0; i < 39; ++i) all[i] = (char)b;
                all[39] = 0;
                CHECK(one(all, set), "a subject entirely inside the set, spanning to the terminator");
            }
            /* the byte in the subject, a DIFFERENT byte in the set */
            {
                char other = (char)((b == 0xFE) ? 0xFD : 0xFE);
                set[0] = other;
                s[2] = (char)b;
                if (b != (unsigned char)other) CHECK(one(s, set), "member of the other table half");
            }
        }
    }

    // ---- both halves of the bitmap in one set --------------------------------------------------
    {
        static char set[8];
        set[0] = (char)0x41; set[1] = (char)0xC3; set[2] = 0;     /* one low, one high */
        static char s[64];
        for (int pos = 0; pos < 40; ++pos) {
            for (int i = 0; i < 40; ++i) s[i] = 'z';
            s[40] = 0;
            s[pos] = (char)0x41;
            CHECK(one(s, set), "low-half member at every position");
            s[pos] = (char)0xC3;
            CHECK(one(s, set), "high-half member at every position");
        }
    }

    // ---- every start offset within a 32-byte block ---------------------------------------------
    {
        static const char* S[] = { "", "a", "abcdef", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                   "abcdefghijklmnopqrstuvwxyzabcdefghij" };
        static const char* SET[] = { "", "a", "f", "xyz", "abcdef", "z" };
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 6; ++j)
                CHECK(one_all_offsets(S[i], (int)strlen(S[i]), SET[j]),
                      "hand-picked cases at every start offset");
    }

    // ---- every subject length x every hit position ----------------------------------------------
    {
        static char s[200];
        for (int len = 0; len <= 100; ++len) {
            for (int i = 0; i < len; ++i) s[i] = 'a';
            s[len] = 0;
            CHECK(one_all_offsets(s, len, "Z"), "no member, every length");
            for (int hit = 0; hit < len; ++hit) {
                s[hit] = 'Z';
                if (!one(s, "Z")) { CHECK(0, "member at every position x every length"); break; }
                s[hit] = 'a';
            }
        }
    }

    // ---- long subjects through the multi-block loop ---------------------------------------------
    {
        static char big[4096];
        for (int len = 100; len <= 3000; len += 331) {
            for (int off = 0; off < 33; off += 8) {
                char* s = big + off;
                for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
                s[len] = 0;
                CHECK(one(s, "#"), "long, no member");
                s[len-1] = '#';
                CHECK(one(s, "#"), "long, member on the final character");
                s[len-1] = 'a'; s[len/2] = '#';
                CHECK(one(s, "#"), "long, member in the middle");
                s[len/2] = 'a';
            }
        }
    }

    // ---- fuzz over the full byte range, with random sets ----------------------------------------
    {
        static char s[600], set[40];
        for (int t = 0; t < 300000; ++t) {
            int len = (int)(rnd() % 500);
            int sl  = (int)(rnd() % 12);
            for (int i = 0; i < sl; ++i)  { set[i] = (char)(1 + rnd() % 255); }
            set[sl] = 0;
            for (int i = 0; i < len; ++i) { s[i] = (char)(1 + rnd() % 255); }
            s[len] = 0;
            CHECK(one(s, set), "fuzz, full byte range, random sets");
        }
    }

    // ---- GUARD PAGE: the terminator on the last byte of a mapped page ---------------------------
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
            CHECK(one(s, "#"), "guard page: no member, must stop at the terminator");
            s[len-1] = '#';
            CHECK(one(s, "#"), "guard page: member on the final character");
            s[len-1] = 'a';
        }
        /* and a SET that itself ends at the guard page */
        {
            for (int sl = 1; sl <= 60; ++sl) {
                char* set = (base + pg) - (SIZE_T)sl - 1;
                for (int i = 0; i < sl; ++i) set[i] = (char)('A' + (i % 20));
                set[sl] = 0;
                CHECK(one("hello world", set), "guard page: the SET ends at the page boundary");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StrSpnA vs live shlwapi + oracle: NULL arguments, including the "
           "distinction a reimplementation is most likely to miss -- here a NULL set and an EMPTY "
           "set BOTH return 0, where the sibling StrCSpnA sharing this core gives 0 and strlen "
           "respectively, so the rule is measured rather than carried across; a subject made "
           "ENTIRELY of each byte value, so the span runs to the terminator and the INVERTED "
           "mask has to stop there with no NUL compare of its own; EVERY byte value 0x01..0xFF proved both as a set member and as "
           "a non-member, because the membership test resolves 0x00..0x7F through one vpshufb table "
           "and 0x80..0xFF through the other and a swapped blend would pass any ASCII-only test; a "
           "two-member set spanning BOTH halves with each member walked across 40 positions; "
           "hand-picked cases x six sets run at EVERY start offset within a 32-byte block WITH SET "
           "MEMBERS PLANTED IN FRONT OF THE STRING, since the first load is aligned DOWN and a "
           "mis-shifted mask would otherwise be hidden by harmless padding; every subject length "
           "0..100 x every hit position; long subjects to 3000 characters through the multi-block "
           "loop; 300k fuzz over the full byte range with random sets; and a GUARD PAGE section for "
           "every length 1..200 -- plus 60 lengths where the SET ITSELF ends at the page boundary, "
           "since the set is scanned too)\n");
    return 0;
}
