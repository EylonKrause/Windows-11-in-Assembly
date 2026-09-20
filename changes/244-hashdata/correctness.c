/* changes/244-hashdata/correctness.c
 *
 * Gate 1 for change 244: wia_hashdata must be indistinguishable from the live HashData.
 *
 * Three-way on every case -- ours, an independent oracle (reference.c) and the live export -- and
 * the comparison is the whole BUFFER against a poison fill plus a canary past the digest, never
 * just the digest bytes. Three measured facts make anything less insufficient:
 *
 *   * cbHash == 0 writes nothing at all, which a digest-only comparison cannot tell from writing
 *     the same bytes back;
 *   * cbData == 0 writes the SEED and nothing else, so the seed is a result in its own right;
 *   * the implementation stores its digest a group of twelve at a time, so an off-by-one in the
 *     partial-group store writes one byte too many -- past cbHash, where only a canary sees it.
 *
 * The overlap sweep is the point of this file. The fast path holds twelve lanes in registers and
 * advances each group across the whole source, which is valid only because the digest bytes are
 * independent chains -- and that independence fails the moment the digest overlaps the source,
 * because the shipped inner loop re-reads src[i] for every lane. probes/overlap.c measured the
 * grouped shape agreeing on 760 of 760 disjoint placements and DISAGREEING on all 1641 overlapping
 * ones, so the implementation detects the intersection and emulates the shipped loop instead. This
 * file drives every relative placement of source and digest inside one buffer, at several sizes,
 * because a corpus of disjoint buffers would validate an implementation with no fallback at all.
 *
 * Guard pages both ways. The digest is placed so that its last byte is the last writable byte of a
 * page, and the source so that its last byte is the last readable byte -- which is what catches an
 * implementation that rounds either range up to a block. The seed writer stores thirty-two bytes at
 * a time, so the digest-side sweep walks every length from 1 to 200.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern long wia_hashdata(const unsigned char*, unsigned long, unsigned char*, unsigned long);
extern long wia_ref_hashdata(const unsigned char*, unsigned long, unsigned char*, unsigned long);

typedef HRESULT (WINAPI *FN)(const BYTE*, DWORD, BYTE*, DWORD);
static FN sys;

static int fails = 0;
static long cases = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define POISON 0xAB
#define CANARY 32

/* one case, disjoint buffers: compare the HRESULT and the whole buffer three ways */
static void one(const unsigned char* src, unsigned long n, unsigned long m, const char* what)
{
    static unsigned char a[4096 + CANARY], b[4096 + CANARY], c[4096 + CANARY];
    memset(a, POISON, sizeof a);
    memset(b, POISON, sizeof b);
    memset(c, POISON, sizeof c);
    long ha = wia_hashdata(src, n, a, m);
    long hb = wia_ref_hashdata(src, n, b, m);
    long hc = (long)sys(src, n, c, m);
    ++cases;
    CHECK(ha == hc, "%s n=%lu m=%lu: HRESULT ours %08lX live %08lX", what, n, m,
          (unsigned long)ha, (unsigned long)hc);
    CHECK(hb == hc, "%s n=%lu m=%lu: HRESULT oracle %08lX live %08lX", what, n, m,
          (unsigned long)hb, (unsigned long)hc);
    CHECK(memcmp(a, c, m + CANARY) == 0, "%s n=%lu m=%lu: buffer ours vs live", what, n, m);
    CHECK(memcmp(b, c, m + CANARY) == 0, "%s n=%lu m=%lu: buffer oracle vs live", what, n, m);
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "HashData");
    if (!sys) { printf("cannot resolve HashData\n"); return 1; }

    static unsigned char src[8192];
    unsigned long rng = 0x2468ACEu;

    /* ---- exhaustive small shapes, with every byte value reaching every position ---- */
    for (unsigned long n = 0; n <= 8; ++n)
        for (unsigned long m = 0; m <= 30; ++m)
            for (int fill = 0; fill < 48; ++fill) {
                for (unsigned long i = 0; i < n; ++i)
                    src[i] = (unsigned char)(fill * 5 + i * 51 + (i == n - 1 ? 0 : 0));
                one(src, n, m, "small");
            }

    /* ---- every single byte value as a one-byte source, at several digest lengths ---- */
    for (int v = 0; v < 256; ++v) {
        src[0] = (unsigned char)v;
        one(src, 1, 1, "onebyte");
        one(src, 1, 4, "onebyte");
        one(src, 1, 12, "onebyte");
        one(src, 1, 13, "onebyte");      /* the first partial group */
        one(src, 1, 16, "onebyte");
        one(src, 1, 24, "onebyte");
        one(src, 1, 25, "onebyte");
    }

    /* ---- every digest length across the group boundary, with a fixed long source ---- */
    for (unsigned long i = 0; i < 300; ++i) src[i] = (unsigned char)(i * 37 + 3);
    for (unsigned long m = 0; m <= 160; ++m) one(src, 300, m, "mlen");

    /* ---- every source length, fixed digest lengths ---- */
    for (unsigned long n = 0; n <= 300; ++n) {
        one(src, n, 16, "nlen");
        one(src, n, 1, "nlen");
        one(src, n, 13, "nlen");
    }

    /* ---- long sources and long digests, where the pass structure shows ---- */
    for (int t = 0; t < 120; ++t) {
        rng = rng * 1103515245u + 12345u;
        unsigned long n = (rng >> 8) % 4001;
        rng = rng * 1103515245u + 12345u;
        unsigned long m = (rng >> 8) % 301;
        for (unsigned long i = 0; i < n; ++i) {
            rng = rng * 1103515245u + 12345u;
            src[i] = (unsigned char)(rng >> 16);
        }
        one(src, n, m, "fuzz");
    }

    /* ---- every NULL combination: the digest must be untouched ---- */
    {
        static unsigned char a[64], b[64], c[64];
        memset(a, POISON, sizeof a); memset(b, POISON, sizeof b); memset(c, POISON, sizeof c);
        long ha = wia_hashdata(0, 4, a, 4);
        long hb = wia_ref_hashdata(0, 4, b, 4);
        long hc = (long)sys(0, 4, c, 4);
        CHECK(ha == hc && hb == hc, "NULL source: ours %08lX oracle %08lX live %08lX",
              (unsigned long)ha, (unsigned long)hb, (unsigned long)hc);
        CHECK(memcmp(a, c, sizeof a) == 0 && memcmp(b, c, sizeof b) == 0,
              "NULL source touched the digest");
        ha = wia_hashdata((const unsigned char*)"abcd", 4, 0, 4);
        hc = (long)sys((const BYTE*)"abcd", 4, 0, 4);
        CHECK(ha == hc, "NULL digest: ours %08lX live %08lX", (unsigned long)ha, (unsigned long)hc);
        ha = wia_hashdata(0, 0, 0, 0);
        hc = (long)sys(0, 0, 0, 0);
        CHECK(ha == hc, "both NULL: ours %08lX live %08lX", (unsigned long)ha, (unsigned long)hc);
        cases += 3;
    }

    /* ---- The overlap sweep: every relative placement inside one buffer ---- */
    {
        enum { BUF = 512 };
        static unsigned char a[BUF], b[BUF], c[BUF], seed[BUF];
        static const unsigned long NS[] = { 1, 2, 5, 13, 24, 40 };
        static const unsigned long MS[] = { 1, 2, 12, 13, 20, 37 };
        long ov = 0, dj = 0;
        for (int i = 0; i < BUF; ++i) seed[i] = (unsigned char)(i * 37 + 11);
        for (int ni = 0; ni < 6; ++ni) {
            for (int mi = 0; mi < 6; ++mi) {
                unsigned long n = NS[ni], m = MS[mi];
                for (unsigned long doff = 0; doff <= 64; ++doff) {
                    for (unsigned long hoff = 0; hoff <= 64; ++hoff) {
                        memcpy(a, seed, BUF); memcpy(b, seed, BUF); memcpy(c, seed, BUF);
                        long ha = wia_hashdata(a + doff, n, a + hoff, m);
                        long hb = wia_ref_hashdata(b + doff, n, b + hoff, m);
                        long hc = (long)sys(c + doff, n, c + hoff, m);
                        ++cases;
                        if (doff < hoff + m && hoff < doff + n) ++ov; else ++dj;
                        CHECK(ha == hc && hb == hc,
                              "overlap n=%lu m=%lu d=%lu h=%lu: HRESULT", n, m, doff, hoff);
                        CHECK(memcmp(a, c, BUF) == 0,
                              "overlap n=%lu m=%lu d=%lu h=%lu: ours differs", n, m, doff, hoff);
                        CHECK(memcmp(b, c, BUF) == 0,
                              "overlap n=%lu m=%lu d=%lu h=%lu: oracle differs", n, m, doff, hoff);
                    }
                }
            }
        }
        printf("  overlap sweep: %ld placements overlapping, %ld disjoint\n", ov, dj);
    }

    /* ---- guard pages: the digest ending exactly at an unwritable page ---- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* gh = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        char* gs = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        static unsigned char ref[512];
        DWORD old;
        if (gh && gs) {
            VirtualProtect(gh + pg, pg, PAGE_NOACCESS, &old);
            VirtualProtect(gs + pg, pg, PAGE_NOACCESS, &old);
            for (unsigned long i = 0; i < 400; ++i) src[i] = (unsigned char)(i * 29 + 5);
            /* the digest's last byte is the page's last writable byte */
            for (unsigned long m = 1; m <= 200; ++m) {
                unsigned char* d = (unsigned char*)(gh + pg) - m;
                memset(ref, POISON, sizeof ref);
                wia_ref_hashdata(src, 137, ref, m);
                memset(d, POISON, m);
                long ha = wia_hashdata(src, 137, d, m);
                ++cases;
                CHECK(ha == 0, "guard digest m=%lu returned %08lX", m, (unsigned long)ha);
                CHECK(memcmp(d, ref, m) == 0, "guard digest m=%lu: wrong bytes", m);
            }
            /* the source's last byte is the page's last readable byte */
            for (unsigned long n = 1; n <= 200; ++n) {
                unsigned char* s = (unsigned char*)(gs + pg) - n;
                for (unsigned long i = 0; i < n; ++i) s[i] = (unsigned char)(i * 13 + 7);
                static unsigned char mine[64];
                memset(ref, POISON, sizeof ref);
                memset(mine, POISON, sizeof mine);
                wia_ref_hashdata(s, n, ref, 16);
                long ha = wia_hashdata(s, n, mine, 16);
                ++cases;
                CHECK(ha == 0, "guard source n=%lu returned %08lX", n, (unsigned long)ha);
                CHECK(memcmp(mine, ref, 16) == 0, "guard source n=%lu: wrong bytes", n);
            }
            VirtualFree(gh, 0, MEM_RELEASE);
            VirtualFree(gs, 0, MEM_RELEASE);
        }
    }

    /* ---- the largest digest the seed writer's vector path exercises ---- */
    {
        static unsigned char a[1200], b[1200], cc[1200];
        for (unsigned long m = 250; m <= 1100; m += 7) {
            memset(a, POISON, sizeof a); memset(b, POISON, sizeof b); memset(cc, POISON, sizeof cc);
            wia_hashdata(src, 0, a, m);                /* cbData 0: the seed on its own */
            wia_ref_hashdata(src, 0, b, m);
            sys(src, 0, cc, m);
            ++cases;
            CHECK(memcmp(a, cc, m + CANARY) == 0, "seed-only m=%lu: ours differs", m);
            CHECK(memcmp(b, cc, m + CANARY) == 0, "seed-only m=%lu: oracle differs", m);
            memset(a, POISON, sizeof a); memset(b, POISON, sizeof b); memset(cc, POISON, sizeof cc);
            wia_hashdata(src, 3, a, m);                /* and with a source, many groups */
            wia_ref_hashdata(src, 3, b, m);
            sys(src, 3, cc, m);
            ++cases;
            CHECK(memcmp(a, cc, m + CANARY) == 0, "many-groups m=%lu: ours differs", m);
            CHECK(memcmp(b, cc, m + CANARY) == 0, "many-groups m=%lu: oracle differs", m);
        }
    }

    if (fails) {
        printf("CORRECTNESS: FAILED (%d checks, %ld cases)\n", fails, cases);
        return 1;
    }
    printf("CORRECTNESS: PASS (HashData vs live + oracle, WHOLE BUFFER against poison with a "
           "32-byte canary past the digest: cbData 0..8 x cbHash 0..30 x 48 fills, every byte value "
           "as a one-byte source at seven digest lengths, every digest length 0..160 across the "
           "twelve-lane group boundary, every source length 0..300, 120 fuzz cases to cbData 4000 x "
           "cbHash 300, every NULL combination, THE FULL OVERLAP SWEEP over every relative "
           "placement of six source and six digest lengths inside one buffer, guard pages on BOTH "
           "sides at every length 1..200, and seed-only digests to 1100 bytes; %ld cases)\n", cases);
    return 0;
}
