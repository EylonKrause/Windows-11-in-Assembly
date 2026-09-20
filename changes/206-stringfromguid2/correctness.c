// changes/206-stringfromguid2/correctness.c
// Gate 1: wia_StringFromGUID2 must be indistinguishable from combase!StringFromGUID2.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// Two things get special attention.
//
// 1. cchMax is SIGNED and every value below 39 is a flat refusal that must leave the buffer
//    UNTOUCHED. An unsigned compare would read -1 as enormous and render into the caller's buffer,
//    so the sweep covers negatives explicitly, and every case compares the whole buffer, not just
//    the return value, so a refusal that scribbled anything would be caught.
// 2. cchMax == 39 is the exact fit. One cell too few is a refusal; one too many is a success that
//    must still write exactly 39 cells and no more, which the page-guard section proves.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_StringFromGUID2(const GUID*, wchar_t*, int);
int ref_StringFromGUID2(const GUID*, wchar_t*, int);
typedef int (WINAPI *FN)(const GUID*, wchar_t*, int);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define PW ((wchar_t)0x2A2A)
#define DSZ 96

static unsigned long sd = 0x206206u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

/* return value AND the whole buffer, three ways */
static int one(const GUID* g, int cch){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    int i;
    for (i = 0; i < DSZ; ++i) { a[i] = PW; b[i] = PW; c[i] = PW; }
    int ra = wia_StringFromGUID2(g, a, cch);
    int rb = ref_StringFromGUID2(g, b, cch);
    int rc = sys(g, c, cch);
    if (ra != rb || ra != rc) return 0;
    for (i = 0; i < DSZ; ++i) if (a[i] != b[i] || a[i] != c[i]) return 0;
    return 1;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"combase.dll");
    sys = (FN)GetProcAddress(h, "StringFromGUID2");
    if (!sys) { h = LoadLibraryW(L"ole32.dll"); sys = (FN)GetProcAddress(h, "StringFromGUID2"); }
    if (!sys) { printf("CORRECTNESS: cannot resolve StringFromGUID2\n"); return 1; }

    static const GUID FIXED[] = {
        {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}},
        {0x00000000,0x0000,0x0000,{0,0,0,0,0,0,0,0}},
        {0xFFFFFFFF,0xFFFF,0xFFFF,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}},
        {0x01020304,0x0506,0x0708,{0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10}},
        {0xABCDEF01,0x2345,0x6789,{0xAB,0xCD,0xEF,0x01,0x23,0x45,0x67,0x89}},
    };
    enum { NF = sizeof(FIXED)/sizeof(FIXED[0]) };

    // ---- every cchMax from -8 to 80 on every fixed GUID: the refusal region, the exact fit, spare
    for (int i = 0; i < NF; ++i)
        for (int cch = -8; cch <= 80; ++cch)
            CHECK(one(&FIXED[i], cch), "every cchMax -8..80");

    // ---- deeply negative and INT_MIN, where an unsigned compare would render ----
    {
        static const int BAD[] = { -1, -2, -39, -40, -1000, -65536, -2147483647-1 };
        for (int i = 0; i < NF; ++i)
            for (int k = 0; k < (int)(sizeof(BAD)/sizeof(BAD[0])); ++k)
                CHECK(one(&FIXED[i], BAD[k]), "negative cchMax must refuse, not render");
    }

    // ---- huge cchMax ----
    {
        static const int BIG[] = { 1000, 65536, 0x7FFFFFFF };
        for (int i = 0; i < NF; ++i)
            for (int k = 0; k < 3; ++k)
                CHECK(one(&FIXED[i], BIG[k]), "huge cchMax");
    }

    // ---- every byte position, every value: proves the print permutation ----
    for (int pos = 0; pos < 16; ++pos) {
        for (int v = 0; v < 256; ++v) {
            GUID g; unsigned char* p = (unsigned char*)&g;
            for (int i = 0; i < 16; ++i) p[i] = (unsigned char)(0x11 * (i & 15));
            p[pos] = (unsigned char)v;
            CHECK(one(&g, 64), "single byte swept, generous buffer");
            CHECK(one(&g, 39), "single byte swept, exact fit");
            CHECK(one(&g, 38), "single byte swept, one short (refusal)");
        }
    }

    // ---- unaligned GUID pointers ----
    {
        static unsigned char raw[64];
        for (int off = 0; off < 16; ++off) {
            memcpy(raw + off, &FIXED[0], 16);
            CHECK(one((const GUID*)(raw + off), 64), "unaligned GUID pointer");
            CHECK(one((const GUID*)(raw + off), 39), "unaligned GUID pointer, exact fit");
        }
    }

    // ---- fuzz over GUID x cchMax ----
    for (int t = 0; t < 300000; ++t) {
        GUID g; unsigned char* p = (unsigned char*)&g;
        for (int i = 0; i < 16; ++i) p[i] = (unsigned char)rnd();
        int cch;
        unsigned s = rnd() % 10;
        if (s < 5)      cch = (int)(rnd() % 50);
        else if (s < 7) cch = 39 + (int)(rnd() % 200);
        else if (s < 9) cch = (int)(rnd() % 3);
        else            cch = -(int)(rnd() % 4096);
        CHECK(one(&g, cch), "fuzz");
    }

    // ---- NULL buffer with a refusing length: must not fault ----
    {
        int ra = wia_StringFromGUID2(&FIXED[0], NULL, 0);
        int rc = sys(&FIXED[0], NULL, 0);
        CHECK(ra == rc, "NULL buffer, cchMax 0: same return, no fault");
        ra = wia_StringFromGUID2(&FIXED[0], NULL, -5);
        rc = sys(&FIXED[0], NULL, -5);
        CHECK(ra == rc, "NULL buffer, negative cchMax");
    }

    // ---- PAGE GUARD: exactly 39 cells before a NOACCESS page, so a 40th cell faults ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
        static wchar_t mirror[DSZ];

        /* success case: exactly 39 cells fit before the guard */
        {
            wchar_t* p = (wchar_t*)((base + pg) - 39 * sizeof(wchar_t));
            for (int i = 0; i < 39; ++i) p[i] = PW;
            int ra = wia_StringFromGUID2(&FIXED[0], p, 39);
            for (int i = 0; i < 39; ++i) mirror[i] = p[i];
            for (int i = 0; i < 39; ++i) p[i] = PW;
            int rc = sys(&FIXED[0], p, 39);
            int ok = (ra == rc);
            if (ok) for (int i = 0; i < 39; ++i) if (mirror[i] != p[i]) { ok = 0; break; }
            CHECK(ok, "page-guard: exactly 39 cells, nothing written past them");
        }
        /* refusal cases: the buffer ends before the guard and must not be touched at all */
        for (int cch = 0; cch <= 38; ++cch) {
            wchar_t* p = (wchar_t*)((base + pg) - (cch ? cch : 1) * sizeof(wchar_t));
            for (int i = 0; i < (cch ? cch : 1); ++i) p[i] = PW;
            int ra = wia_StringFromGUID2(&FIXED[0], p, cch);
            int ok = (ra == 0);
            for (int i = 0; i < (cch ? cch : 1); ++i) if (p[i] != PW) { ok = 0; break; }
            CHECK(ok, "page-guard: a refusal writes nothing");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StringFromGUID2 vs live combase + oracle -- return value AND the "
           "whole buffer on every case, including the refusals, since cchMax < 39 must leave the "
           "buffer untouched: 5 fixed GUIDs x every cchMax -8..80, 7 deeply negative lengths "
           "including INT_MIN (where an unsigned compare would render into the caller's buffer), "
           "huge lengths up to INT_MAX, EVERY byte position x all 256 values x 3 length regimes "
           "(which proves the 3,2,1,0,5,4,7,6,8..15 print permutation), 16 unaligned GUID pointers, "
           "300k fuzz, a NULL buffer with refusing lengths, and a PAGE_NOACCESS guard proving both "
           "that an exact 39-cell fit writes no 40th cell and that every refusal writes nothing at "
           "all)\n");
    return 0;
}
