// changes/211-lstrcpyna/correctness.c
// Gate 1: wia_lstrcpyna must be indistinguishable from kernelbase!lstrcpynA.
//
// Three-way: our assembly+SEH wrapper vs the scalar oracle vs the LIVE export on this PC.
//
// THREE THINGS DRIVE THE SHAPE OF THIS TEST.
//
// 1. The destination is TERMINATED, NOT PADDED. Every case therefore compares the WHOLE destination
//    buffer against a poison fill, not just the copied prefix -- a strncpy-shaped implementation
//    would zero-fill the tail and pass a prefix-only check.
// 2. A FAULTING SOURCE IS PART OF THE CONTRACT: it returns NULL with the readable prefix already
//    copied. The page-guard section below builds exactly that -- an unterminated string ending at a
//    PAGE_NOACCESS boundary -- and compares our result against the LIVE export byte for byte,
//    including how much of the destination each one filled in before giving up. That is what the
//    page-safe copy in impl.asm exists to get right; a 32-byte load straddling the boundary would
//    fault before storing and leave less behind.
// 3. A NARROW character is ONE BYTE, so the source can sit at ANY offset in a page and a 32-byte
//    chunk covers 32 characters rather than 16. Both change where the page-safe path engages, so the
//    page-guard section walks every source length 1..96 -- three full chunk widths -- rather than
//    relying on the two-byte alignment the wide form always had.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern char* wia_lstrcpyna(char*, const char*, int);
char* ref_lstrcpyna(char*, const char*, int);
typedef char* (WINAPI *FN)(char*, const char*, int);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define PB ((char)0x2A)
#define DSZ 640

/* return value AND the whole destination buffer, three ways */
static int one(const char* src, int n){
    static char a[DSZ], b[DSZ], c[DSZ];
    int i;
    for (i = 0; i < DSZ; ++i) { a[i] = PB; b[i] = PB; c[i] = PB; }
    char* ra = wia_lstrcpyna(a, src, n);
    char* rb = ref_lstrcpyna(b, src, n);
    char* rc = sys(c, src, n);
    int oka = (ra == a), okb = (rb == b), okc = (rc == c);
    if (oka != okb || oka != okc) return 0;
    for (i = 0; i < DSZ; ++i) if (a[i] != b[i] || a[i] != c[i]) return 0;
    return 1;
}

static unsigned long sd = 0x211211u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "lstrcpynA");
    if (!sys) { h = LoadLibraryW(L"kernel32.dll"); sys = (FN)GetProcAddress(h, "lstrcpynA"); }
    if (!sys) { printf("CORRECTNESS: cannot resolve lstrcpynA\n"); return 1; }

    // ---- NULL arguments ----
    {
        static char d[DSZ];
        for (int i = 0; i < DSZ; ++i) d[i] = PB;
        CHECK(wia_lstrcpyna(d, NULL, 8) == sys(d, NULL, 8), "NULL source returns the same thing");
        CHECK(d[0] == PB, "NULL source touches nothing");
        CHECK((wia_lstrcpyna(NULL, "abc", 8) == NULL) == (sys(NULL, "abc", 8) == NULL),
              "NULL destination");
        CHECK((wia_lstrcpyna(NULL, NULL, 8) == NULL) == (sys(NULL, NULL, 8) == NULL), "both NULL");
    }

    // ---- every source length 0..160 x every n 0..168, which covers the exact fit, one short,
    // ---- n == 0 (writes nothing), n == 1 (terminator only), every truncation point, and -- because
    // ---- a chunk is 32 NARROW characters -- five full chunk widths rather than the wide form's two.
    {
        static char src[192];
        for (int sl = 0; sl <= 160; ++sl) {
            for (int i = 0; i < sl; ++i) src[i] = (char)('a' + (i % 26));
            src[sl] = 0;
            for (int n = 0; n <= 168; ++n)
                CHECK(one(src, n), "every source length 0..160 x every n 0..168");
        }
    }

    // ---- n used UNSIGNED: negatives copy the whole string rather than meaning "empty" ----
    {
        static const int NEG[] = { -1, -2, -100, -1000, -65536, -2147483647-1 };
        for (int k = 0; k < 6; ++k) {
            CHECK(one("abcdefgh", NEG[k]), "negative n copies everything (unsigned)");
            CHECK(one("", NEG[k]), "negative n, empty source");
        }
    }

    // ---- EVERY byte offset through a 32-byte chunk, for source and destination independently. A
    // ---- narrow character imposes no alignment at all, so the chunked path has to be correct from
    // ---- any starting address, not merely from an even one.
    {
        static char raw[1024];
        static char da[DSZ], db[DSZ], dc[DSZ];
        for (int i = 0; i < 900; ++i) raw[i] = (char)('A' + (i % 26));
        for (int off = 0; off < 40; ++off) {
            raw[off + 200] = 0;
            CHECK(one(raw + off, 300), "unaligned source offsets");
            CHECK(one(raw + off, 100), "unaligned source, truncating");
            raw[off + 200] = (char)('A' + ((off + 200) % 26));
        }
        /* and an unaligned DESTINATION, which `one` cannot express because it owns its buffers */
        for (int doff = 0; doff < 40; ++doff) {
            for (int sl = 0; sl <= 100; sl += 7) {
                for (int i = 0; i < sl; ++i) raw[i] = (char)('a' + (i % 26));
                raw[sl] = 0;
                int i;
                for (i = 0; i < DSZ; ++i) { da[i] = PB; db[i] = PB; dc[i] = PB; }
                char* ra = wia_lstrcpyna(da + doff, raw, 200);
                char* rb = ref_lstrcpyna(db + doff, raw, 200);
                char* rc = sys(dc + doff, raw, 200);
                int ok = ((ra == da + doff) == (rc == dc + doff))
                      && ((rb == db + doff) == (rc == dc + doff));
                if (ok) for (i = 0; i < DSZ; ++i)
                            if (da[i] != dc[i] || db[i] != dc[i]) { ok = 0; break; }
                CHECK(ok, "unaligned destination offsets");
            }
        }
    }

    // ---- long sources, to exercise the 32-character chunked path and its tail ----
    {
        static char big[600];
        for (int len = 100; len <= 500; len += 37) {
            for (int i = 0; i < len; ++i) big[i] = (char)('a' + (i % 26));
            big[len] = 0;
            CHECK(one(big, len + 1), "long source, exact fit");
            CHECK(one(big, len),     "long source, one short");
            CHECK(one(big, len / 2), "long source, halved");
            CHECK(one(big, 600),     "long source, room to spare");
        }
    }

    // ---- fuzz, over the FULL byte range including the high half. The ANSI code page is 1252 with
    // ---- no DBCS lead bytes, so 0x80..0xFF are ordinary characters here and must copy as such.
    {
        static char src[600];
        for (int t = 0; t < 200000; ++t) {
            int sl = (int)(rnd() % 560);
            for (int i = 0; i < sl; ++i) src[i] = (char)(1 + rnd() % 255);
            src[sl] = 0;
            int n = (int)(rnd() % 600);
            CHECK(one(src, n), "fuzz");
        }
    }

    // ---- PAGE GUARD, the case the whole design turns on -----------------------------------------
    // An UNTERMINATED source ending exactly at a PAGE_NOACCESS page. The live export swallows the
    // fault and returns NULL with the readable prefix already copied; ours must do the same, and
    // must have copied exactly as much. The oracle cannot model a fault, so this compares against
    // the LIVE export only. EVERY bound 1..n is walked, not a sampled subset, because the decisive
    // case is the single value n == srclen+1 where the shipped loop reads one past what it copies.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        static char da[DSZ], dc[DSZ];
        for (int sl = 1; sl <= 96; ++sl) {
            char* s = (base + pg) - (SIZE_T)sl;
            for (int i = 0; i < sl; ++i) s[i] = (char)('a' + (i % 26));   /* NO terminator */
            for (int n = 1; n <= 130; ++n) {
                int i;
                for (i = 0; i < DSZ; ++i) { da[i] = PB; dc[i] = PB; }
                char* ra = wia_lstrcpyna(da, s, n);
                char* rc = sys(dc, s, n);
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
    printf("CORRECTNESS: PASS (lstrcpynA vs live kernelbase + oracle -- the return value AND the "
           "WHOLE destination buffer on every case, because the destination is TERMINATED and NOT "
           "PADDED and a strncpy-shaped implementation would pass a prefix-only check: NULL "
           "arguments, EVERY source length 0..160 x every n 0..168 (covering n==0 which writes "
           "nothing at all, n==1 which writes only a terminator, every truncation point, and five "
           "full 32-character chunk widths), six negative lengths down to INT_MIN proving n is used "
           "UNSIGNED, 40 unaligned SOURCE offsets and 40 unaligned DESTINATION offsets because a "
           "narrow character imposes no alignment whatsoever, long sources through the chunked path, "
           "200k fuzz over the FULL byte range including 0x80..0xFF which are ordinary characters in "
           "code page 1252, and -- the case the design turns on -- an UNTERMINATED source ending at "
           "a PAGE_NOACCESS page for EVERY length 1..96 x EVERY bound 1..130, where the live export "
           "swallows the fault and returns NULL with a partial copy, and ours must match both the "
           "return AND exactly how much it managed to copy first)\n");
    return 0;
}
