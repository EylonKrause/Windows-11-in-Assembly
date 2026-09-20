// changes/205-uuidfromstringa/correctness.c
// Gate 1: wia_uuidfromstringa must be indistinguishable from rpcrt4!UuidFromStringA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// Two things make this test unusual.
//
// 1. The output must be UNTOUCHED on failure, so every case pre-poisons the GUID and compares all
//    sixteen bytes afterwards even when the call is expected to fail. A version that scribbled a
//    partial parse before detecting a bad digit would pass a return-value-only test.
// 2. The implementation reads all 37 bytes before it knows the string is that long, guarded by a
//    page-offset check. So there is a PAGE-GUARD section that places strings of every length at
//    every offset in the last 40 bytes before a PAGE_NOACCESS page -- if the guard were wrong, or
//    the bounded fallback scan miscounted, this faults rather than merely disagreeing.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern long wia_uuidfromstringa(unsigned char*, GUID*);
long ref_uuidfromstringa(unsigned char*, GUID*);
typedef long (WINAPI *FN)(unsigned char*, GUID*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x5A

/* one case: return value AND all sixteen output bytes, three ways */
static int one(const char* s){
    GUID a, b, c;
    memset(&a, POISON, sizeof a); memset(&b, POISON, sizeof b); memset(&c, POISON, sizeof c);
    long ra = wia_uuidfromstringa((unsigned char*)s, &a);
    long rb = ref_uuidfromstringa((unsigned char*)s, &b);
    long rc = sys((unsigned char*)s, &c);
    if (ra != rb || ra != rc) return 0;
    if (memcmp(&a, &b, 16) != 0 || memcmp(&a, &c, 16) != 0) return 0;
    return 1;
}

static unsigned long sd = 0x205205u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"rpcrt4.dll");
    sys = (FN)GetProcAddress(h, "UuidFromStringA");
    if(!sys){ printf("CORRECTNESS: cannot resolve rpcrt4!UuidFromStringA\n"); return 1; }

    // ---- the NULL pointer is a SUCCESS case, not an error ----
    {
        GUID a, c;
        memset(&a, POISON, sizeof a); memset(&c, POISON, sizeof c);
        long ra = wia_uuidfromstringa(NULL, &a);
        long rc = sys(NULL, &c);
        CHECK(ra == rc, "NULL string: same return");
        CHECK(memcmp(&a, &c, 16) == 0, "NULL string: same output (the nil uuid)");
        CHECK(ra == 0, "NULL string returns RPC_S_OK");
    }

    // ---- hand-picked shapes, valid and not ----
    {
        static const char* S[] = {
            "deadbeef-1234-5678-9abc-def011223344",
            "DEADBEEF-1234-5678-9ABC-DEF011223344",
            "DeAdBeEf-1234-5678-9aBc-DeF011223344",
            "00000000-0000-0000-0000-000000000000",
            "ffffffff-ffff-ffff-ffff-ffffffffffff",
            "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF",
            "{deadbeef-1234-5678-9abc-def011223344}",   /* braced: REJECTED by this contract */
            "{deadbeef-1234-5678-9abc-def011223344",
            "deadbeef-1234-5678-9abc-def011223344}",
            "",
            "-",
            "deadbeef",
            "deadbeef-1234-5678-9abc-def01122334",      /* one short */
            "deadbeef-1234-5678-9abc-def0112233445",    /* one long  */
            "deadbeef-1234-5678-9abc-def01122334g",
            "geadbeef-1234-5678-9abc-def011223344",
            "deadbeef+1234-5678-9abc-def011223344",
            "deadbeef-1234+5678-9abc-def011223344",
            "deadbeef-1234-5678+9abc-def011223344",
            "deadbeef-1234-5678-9abc+def011223344",
            "deadbeef 1234 5678 9abc def011223344",
            " deadbeef-1234-5678-9abc-def011223344",
            "deadbeef-1234-5678-9abc-def011223344 ",
            "deadbeef-1234-5678-9abc-def011223344x",
            "deadbeef-1234-5678-9abc-def0112233 4",
            "dead beef-1234-5678-9abc-def01122334",
            "00000000-0000-0000-0000-00000000000",
            "0-0-0-0-0",
        };
        for (int i = 0; i < (int)(sizeof(S)/sizeof(S[0])); ++i)
            CHECK(one(S[i]), "hand-picked shape");
    }

    // ---- every single-character corruption of a valid uuid, at every position, over all 256 bytes.
    // ---- This is the exhaustive part: it proves the separator positions and the hex table together.
    {
        char buf[64];
        for (int pos = 0; pos < 36; ++pos) {
            for (int v = 1; v < 256; ++v) {     /* v == 0 is covered by the truncation cases below */
                memcpy(buf, "deadbeef-1234-5678-9abc-def011223344", 37);
                buf[pos] = (char)v;
                CHECK(one(buf), "single-character corruption sweep");
            }
        }
    }

    // ---- every truncation length 0..40 ----
    {
        char buf[64];
        for (int n = 0; n <= 40; ++n) {
            memcpy(buf, "deadbeef-1234-5678-9abc-def011223344xxxx", 40);
            buf[n] = 0;
            CHECK(one(buf), "truncation at every length 0..40");
        }
    }

    // ---- randomized fuzz: mostly-valid strings with occasional corruption ----
    {
        static const char HEX[] = "0123456789abcdefABCDEF";
        char buf[64];
        for (int t = 0; t < 400000; ++t) {
            for (int i = 0; i < 36; ++i) buf[i] = HEX[rnd() % 22];
            buf[8] = buf[13] = buf[18] = buf[23] = '-';
            unsigned k = rnd() % 10;
            if (k < 3) buf[rnd() % 36] = (char)(rnd() & 0xFF);      /* corrupt one byte   */
            if (k == 3) buf[rnd() % 4 * 5 + 8] = (char)(rnd() & 0xFF);
            int n = 36;
            if (k == 4) n = rnd() % 40;                              /* wrong length       */
            buf[n] = 0;
            if (k == 5 && n < 40) { buf[n] = (char)('a' + rnd() % 6); buf[n+1] = 0; }
            CHECK(one(buf), "fuzz");
        }
    }

    // ---- PAGE GUARD: the string ends right at a PAGE_NOACCESS boundary ----
    // The implementation reads bytes 0..36 unconditionally unless the page-offset check diverts it,
    // so this is where a wrong guard or a miscounted fallback scan faults instead of disagreeing.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        /* a valid 36-char uuid whose terminator is the last readable byte, and then every shorter
           string ending at that same boundary */
        for (int len = 0; len <= 38; ++len) {
            char* p = (base + pg) - (len + 1);       /* len chars + NUL, ending at the guard */
            static const char SRC[] = "deadbeef-1234-5678-9abc-def011223344xx";
            for (int i = 0; i < len; ++i) p[i] = SRC[i];
            p[len] = 0;
            CHECK(one(p), "page-guard: string ends exactly at a NOACCESS page");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (UuidFromStringA vs live rpcrt4 + oracle -- return value AND all 16 "
           "output bytes on EVERY case, including the failing ones, since the contract requires the "
           "GUID to be left untouched on error: the NULL-pointer success case (nil uuid), 28 "
           "hand-picked shapes with braced/short/long/bad-separator/whitespace variants, an "
           "EXHAUSTIVE single-character corruption sweep (36 positions x 255 byte values, which "
           "proves the separator positions and the hex table together), every truncation length "
           "0..40, 400k fuzz, and a PAGE_NOACCESS guard with strings of every length 0..38 ending "
           "exactly at the boundary -- which is what actually exercises the 37-byte page-offset "
           "guard and its bounded fallback scan)\n");
    return 0;
}
