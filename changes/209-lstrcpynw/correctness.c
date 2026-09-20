// changes/209-lstrcpynw/correctness.c
// Gate 1: wia_lstrcpynw must be indistinguishable from kernelbase!lstrcpynW.
//
// Three-way: our assembly+SEH wrapper vs the scalar oracle vs the LIVE export on this PC.
//
// Two things drive the shape of this test.
//
// 1. The destination is terminated, not padded. Every case therefore compares the whole destination
//    buffer against a poison fill, not just the copied prefix -- a strncpy-shaped implementation
//    would zero-fill the tail and pass a prefix-only check.
// 2. a faulting source is part of the contract: it returns NULL with the readable prefix already
//    copied. The page-guard section below builds exactly that -- an unterminated string ending at a
//    PAGE_NOACCESS boundary -- and compares our result against the LIVE export character for
//    character, including how much of the destination each one filled in before giving up. That is
//    what the page-safe copy in impl.asm exists to get right; a 32-byte load straddling the boundary
//    would fault before storing and leave less behind.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern wchar_t* wia_lstrcpynw(wchar_t*, const wchar_t*, int);
wchar_t* ref_lstrcpynw(wchar_t*, const wchar_t*, int);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*, int);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define PW ((wchar_t)0x2A2A)
#define DSZ 320

/* return value AND the whole destination buffer, three ways */
static int one(const wchar_t* src, int n){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    int i;
    for (i = 0; i < DSZ; ++i) { a[i] = PW; b[i] = PW; c[i] = PW; }
    wchar_t* ra = wia_lstrcpynw(a, src, n);
    wchar_t* rb = ref_lstrcpynw(b, src, n);
    wchar_t* rc = sys(c, src, n);
    int oka = (ra == a), okb = (rb == b), okc = (rc == c);
    if (oka != okb || oka != okc) return 0;
    for (i = 0; i < DSZ; ++i) if (a[i] != b[i] || a[i] != c[i]) return 0;
    return 1;
}

static unsigned long sd = 0x209209u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "lstrcpynW");
    if (!sys) { h = LoadLibraryW(L"kernel32.dll"); sys = (FN)GetProcAddress(h, "lstrcpynW"); }
    if (!sys) { printf("CORRECTNESS: cannot resolve lstrcpynW\n"); return 1; }

    // ---- NULL arguments ----
    {
        static wchar_t d[DSZ];
        for (int i = 0; i < DSZ; ++i) d[i] = PW;
        CHECK(wia_lstrcpynw(d, NULL, 8) == sys(d, NULL, 8), "NULL source returns the same thing");
        CHECK(d[0] == PW, "NULL source touches nothing");
        CHECK((wia_lstrcpynw(NULL, L"abc", 8) == NULL) == (sys(NULL, L"abc", 8) == NULL),
              "NULL destination");
        CHECK((wia_lstrcpynw(NULL, NULL, 8) == NULL) == (sys(NULL, NULL, 8) == NULL), "both NULL");
    }

    // ---- every source length 0..80 x every n 0..84, which covers the exact fit, one short,
    // ---- n == 0 (writes nothing), n == 1 (terminator only) and every truncation point.
    {
        static wchar_t src[96];
        for (int sl = 0; sl <= 80; ++sl) {
            for (int i = 0; i < sl; ++i) src[i] = (wchar_t)(L'a' + (i % 26));
            src[sl] = 0;
            for (int n = 0; n <= 84; ++n)
                CHECK(one(src, n), "every source length 0..80 x every n 0..84");
        }
    }

    // ---- n used UNSIGNED: negatives copy the whole string rather than meaning "empty" ----
    {
        static const int NEG[] = { -1, -2, -100, -1000, -65536, -2147483647-1 };
        for (int k = 0; k < 6; ++k) {
            CHECK(one(L"abcdefgh", NEG[k]), "negative n copies everything (unsigned)");
            CHECK(one(L"", NEG[k]), "negative n, empty source");
        }
    }

    // ---- unaligned sources and destinations, which change where the page-safe path kicks in ----
    {
        static wchar_t raw[512];
        for (int i = 0; i < 400; ++i) raw[i] = (wchar_t)(L'A' + (i % 26));
        for (int off = 0; off < 40; ++off) {
            raw[off + 100] = 0;
            CHECK(one(raw + off, 150), "unaligned source offsets");
            CHECK(one(raw + off, 50),  "unaligned source, truncating");
            raw[off + 100] = (wchar_t)(L'A' + ((off + 100) % 26));
        }
    }

    // ---- long sources, to exercise the 16-character chunked path and its tail ----
    {
        static wchar_t big[600];
        for (int len = 100; len <= 500; len += 37) {
            for (int i = 0; i < len; ++i) big[i] = (wchar_t)(L'a' + (i % 26));
            big[len] = 0;
            CHECK(one(big, len + 1), "long source, exact fit");
            CHECK(one(big, len),     "long source, one short");
            CHECK(one(big, len / 2), "long source, halved");
            CHECK(one(big, 600),     "long source, room to spare");
        }
    }

    // ---- fuzz ----
    {
        static wchar_t src[300];
        for (int t = 0; t < 200000; ++t) {
            int sl = (int)(rnd() % 260);
            for (int i = 0; i < sl; ++i) src[i] = (wchar_t)(1 + rnd() % 0xFFFE);
            src[sl] = 0;
            int n = (int)(rnd() % 300);
            CHECK(one(src, n), "fuzz");
        }
    }

    // ---- PAGE GUARD, the case the whole design turns on -----------------------------------------
    // An UNTERMINATED source ending exactly at a PAGE_NOACCESS page. The live export swallows the
    // fault and returns NULL with the readable prefix already copied; ours must do the same, and
    // must have copied exactly as much. The oracle cannot model a fault, so this compares against
    // the LIVE export only.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        static wchar_t da[DSZ], dc[DSZ];
        for (int sl = 1; sl <= 80; ++sl) {
            wchar_t* s = (wchar_t*)((base + pg) - (SIZE_T)sl * sizeof(wchar_t));
            for (int i = 0; i < sl; ++i) s[i] = (wchar_t)(L'a' + (i % 26));   /* NO terminator */
            for (int n = 1; n <= 120; n += 7) {
                int i;
                for (i = 0; i < DSZ; ++i) { da[i] = PW; dc[i] = PW; }
                wchar_t* ra = wia_lstrcpynw(da, s, n);
                wchar_t* rc = sys(dc, s, n);
                int oka = (ra == da), okc = (rc == dc);
                int ok = (oka == okc);
                if (ok) for (i = 0; i < DSZ; ++i) if (da[i] != dc[i]) { ok = 0; break; }
                CHECK(ok, "page-guard: unterminated source at a NOACCESS page "
                          "(return AND how much was copied before giving up)");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (lstrcpynW vs live kernelbase + oracle -- the return value AND the "
           "WHOLE destination buffer on every case, because the destination is TERMINATED and NOT "
           "PADDED and a strncpy-shaped implementation would pass a prefix-only check: NULL "
           "arguments, EVERY source length 0..80 x every n 0..84 (covering n==0 which writes "
           "nothing at all, n==1 which writes only a terminator, and every truncation point), six "
           "negative lengths down to INT_MIN proving n is used UNSIGNED, 40 unaligned source "
           "offsets, long sources through the 16-character chunked path, 200k fuzz, and -- the case "
           "the design turns on -- an UNTERMINATED source ending at a PAGE_NOACCESS page for every "
           "length 1..80 x 18 bounds, where the live export swallows the fault and returns NULL "
           "with a partial copy, and ours must match both the return AND exactly how much it "
           "managed to copy first)\n");
    return 0;
}
