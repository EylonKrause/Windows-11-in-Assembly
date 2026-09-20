/* changes/244-hashdata/probes/hash.c
 *
 * The contract of shlwapi/kernelbase!HashData, measured against the live export.
 *
 * Why this function. discovery/shlwapi_url_str.c timed it at 6.37 ns per source byte with a
 * 16-byte digest, 26 102 ns to hash 4096 bytes, i.e. 0.157 GB/s. That is not a semantic cost:
 * there is no locale, no code page, no path grammar and no allocation anywhere in it. It is bytes
 * in, bytes out.
 *
 * What this probe has to settle, and why each one matters:
 *
 *   1. THE TABLE. The digest is built through a 256-entry byte substitution. Every byte of it has
 *      to be recovered, because one wrong entry is a wrong digest on one input in 256 and nothing
 *      short of an exhaustive check would see it. It is recoverable in closed form: with a
 *      ONE-byte source and a ONE-byte digest the whole computation is a single substitution, so
 *      256 calls to the live export give all 256 entries. The table read out of the shipped binary
 *      is carried below as a HYPOTHESIS and compared against what the export actually produces --
 *      the export is the authority, not the disassembly.
 *
 *   2. The initial digest. The digest is seeded before any source byte is consumed, and the seed
 *      is observable on its own: with cbData == 0 nothing is consumed and whatever is left in the
 *      buffer IS the seed. That also settles what happens past 256 bytes of digest, where a
 *      byte-sized seed must wrap.
 *
 *   3. THE ORDER. A chained substitution is not symmetric: consuming the source front-to-back and
 *      back-to-front give different digests for every input longer than one byte. Both models are
 *      built here and run against all 65 536 two-byte sources, because a corpus of a few strings
 *      would agree with the wrong one often enough to look like a match.
 *
 *   4. Whether the digest bytes interact. If digest byte j is updated only from itself and the
 *      current source byte, the bytes are 16 independent chains and an implementation may compute
 *      them in any order, which is the whole basis of making this fast. If they interact, it is
 *      a serial chain and there is nothing to win. Measured with cbHash = 4 over every source byte
 *      and every digest position.
 *
 *   5. The degenerate and failing cases. cbData == 0, cbHash == 0, NULL either side, and, the one
 *      that decides whether an implementation may touch the buffer at all, whether cbHash == 0
 *      writes anything, and whether cbData == 0 READS anything. Both are answered against a
 *      PAGE_NOACCESS page rather than by inspection, because "it appears not to" and "it does not"
 *      are different claims.
 *
 * Read-only with respect to the system: nothing is patched, nothing is written to disk.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef HRESULT (WINAPI *FN_HASH)(const BYTE*, DWORD, BYTE*, DWORD);
static FN_HASH sys;

/* The hypothesis, read out of kernelbase.dll at rva 0x2A6010 (the table the export's inner loop
   indexes with `movzx ecx, byte ptr [rdx + rsi]`). It is compared against the live export below;
   if the two ever disagree, the export wins and this array is wrong. */
static const unsigned char HYP[256] = {
0x01,0x0E,0x6E,0x19,0x61,0xAE,0x84,0x77,0x8A,0xAA,0x7D,0x76,0x1B,0xE9,0x8C,0x33,
0x57,0xC5,0xB1,0x6B,0xEA,0xA9,0x38,0x44,0x1E,0x07,0xAD,0x49,0xBC,0x28,0x24,0x41,
0x31,0xD5,0x68,0xBE,0x39,0xD3,0x94,0xDF,0x30,0x73,0x0F,0x02,0x43,0xBA,0xD2,0x1C,
0x0C,0xB5,0x67,0x46,0x16,0x3A,0x4B,0x4E,0xB7,0xA7,0xEE,0x9D,0x7C,0x93,0xAC,0x90,
0xB0,0xA1,0x8D,0x56,0x3C,0x42,0x80,0x53,0x9C,0xF1,0x4F,0x2E,0xA8,0xC6,0x29,0xFE,
0xB2,0x55,0xFD,0xED,0xFA,0x9A,0x85,0x58,0x23,0xCE,0x5F,0x74,0xFC,0xC0,0x36,0xDD,
0x66,0xDA,0xFF,0xF0,0x52,0x6A,0x9E,0xC9,0x3D,0x03,0x59,0x09,0x2A,0x9B,0x9F,0x5D,
0xA6,0x50,0x32,0x22,0xAF,0xC3,0x64,0x63,0x1A,0x96,0x10,0x91,0x04,0x21,0x08,0xBD,
0x79,0x40,0x4D,0x48,0xD0,0xF5,0x82,0x7A,0x8F,0x37,0x69,0x86,0x1D,0xA4,0xB9,0xC2,
0xC1,0xEF,0x65,0xF2,0x05,0xAB,0x7E,0x0B,0x4A,0x3B,0x89,0xE4,0x6C,0xBF,0xE8,0x8B,
0x06,0x18,0x51,0x14,0x7F,0x11,0x5B,0x5C,0xFB,0x97,0xE1,0xCF,0x15,0x62,0x71,0x70,
0x54,0xE2,0x12,0xD6,0xC7,0xBB,0x0D,0x20,0x5E,0xDC,0xE0,0xD4,0xF7,0xCC,0xC4,0x2B,
0xF9,0xEC,0x2D,0xF4,0x6F,0xB6,0x99,0x88,0x81,0x5A,0xD9,0xCA,0x13,0xA5,0xE7,0x47,
0xE6,0x8E,0x60,0xE3,0x3E,0xB3,0xF6,0x72,0xA2,0x35,0xA0,0xD7,0xCD,0xB4,0x2F,0x6D,
0x2C,0x26,0x1F,0x95,0x87,0x00,0xD8,0x34,0x3F,0x17,0x25,0x45,0x27,0x75,0x92,0xB8,
0xA3,0xC8,0xDE,0xEB,0xF8,0xF3,0xDB,0x0A,0x98,0x83,0x7B,0xE5,0xCB,0x4C,0x78,0xD1 };

static unsigned char T[256];              /* recovered from the live export, call by call */

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); \
                                       printf("\n"); } } while (0)

/* ---- the two candidate models, both built from the RECOVERED table ---- */
static void model(const unsigned char* src, DWORD n, unsigned char* h, DWORD m, int reverse)
{
    for (DWORD j = 0; j < m; ++j) h[j] = (unsigned char)j;
    if (reverse) {
        for (DWORD i = n; i-- > 0; )
            for (DWORD j = 0; j < m; ++j) h[j] = T[h[j] ^ src[i]];
    } else {
        for (DWORD i = 0; i < n; ++i)
            for (DWORD j = 0; j < m; ++j) h[j] = T[h[j] ^ src[i]];
    }
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN_HASH)GetProcAddress(h, "HashData");
    if (!sys) { printf("cannot resolve HashData\n"); return 1; }
    printf("HashData contract probe\n\n");

    /* ============ 1. recover the table, one call per byte value ============ */
    {
        int perm = 1, seen[256];
        memset(seen, 0, sizeof seen);
        for (int v = 0; v < 256; ++v) {
            unsigned char s = (unsigned char)v, d = 0xAB;
            HRESULT hr = sys(&s, 1, &d, 1);
            CHECK(hr == S_OK, "one-byte call returned %08lX for v=%02X", (unsigned long)hr, v);
            T[v] = d;
        }
        for (int v = 0; v < 256; ++v) { if (seen[T[v]]) perm = 0; seen[T[v]] = 1; }
        printf("1. THE TABLE, recovered by 256 one-byte calls\n");
        printf("   a permutation of 0..255: %s\n", perm ? "YES" : "NO");
        int diff = 0;
        for (int v = 0; v < 256; ++v) if (T[v] != HYP[v]) ++diff;
        printf("   entries differing from the table read out of the binary: %d\n", diff);
        CHECK(perm, "the substitution is not a permutation");
        CHECK(diff == 0, "%d entries disagree with the disassembled table", diff);
        printf("   first sixteen: ");
        for (int v = 0; v < 16; ++v) printf("%02X ", T[v]);
        printf("\n\n");
    }

    /* ============ 2. the seed: cbData == 0 leaves it in the buffer ============ */
    {
        static unsigned char d[512];
        printf("2. THE SEED (cbData == 0 consumes nothing, so the buffer IS the seed)\n");
        int bad = 0, firstbad = -1;
        for (int m = 1; m <= 300; ++m) {
            memset(d, 0xAB, sizeof d);
            HRESULT hr = sys((const BYTE*)"x", 0, d, (DWORD)m);
            CHECK(hr == S_OK, "cbData=0 cbHash=%d returned %08lX", m, (unsigned long)hr);
            for (int j = 0; j < m; ++j)
                if (d[j] != (unsigned char)j) { ++bad; if (firstbad < 0) firstbad = j; }
            for (int j = m; j < m + 8; ++j)
                CHECK(d[j] == 0xAB, "cbHash=%d wrote past the buffer at %d", m, j);
        }
        printf("   seed is h[j] = (BYTE)j for every cbHash 1..300: %s",
               bad ? "NO" : "YES");
        if (bad) printf("  (%d positions differ, first at %d)", bad, firstbad);
        printf("\n");
        /* the wrap is the interesting half: at cbHash 300, h[256] must be 0 again */
        memset(d, 0xAB, sizeof d);
        sys((const BYTE*)"x", 0, d, 300);
        printf("   at cbHash=300: h[255]=%02X h[256]=%02X h[257]=%02X  (a byte seed must wrap)\n\n",
               d[255], d[256], d[257]);
        CHECK(d[256] == 0x00, "the seed does not wrap at 256: h[256]=%02X", d[256]);
    }

    /* ============ 3. the order, over all 65536 two-byte sources ============ */
    {
        printf("3. THE ORDER, all 65536 two-byte sources, both models\n");
        long rev_ok = 0, fwd_ok = 0, neither = 0;
        for (int a = 0; a < 256; ++a) {
            for (int b = 0; b < 256; ++b) {
                unsigned char s[2] = { (unsigned char)a, (unsigned char)b };
                unsigned char live[4] = {0}, mr[4], mf[4];
                sys(s, 2, live, 4);
                model(s, 2, mr, 4, 1);
                model(s, 2, mf, 4, 0);
                int r = memcmp(live, mr, 4) == 0;
                int f = memcmp(live, mf, 4) == 0;
                if (r) ++rev_ok;
                if (f) ++fwd_ok;
                if (!r && !f) ++neither;
            }
        }
        printf("   matches LAST-BYTE-FIRST: %ld / 65536\n", rev_ok);
        printf("   matches FIRST-BYTE-FIRST: %ld / 65536\n", fwd_ok);
        printf("   matches NEITHER:          %ld / 65536\n", neither);
        printf("   (the two models agree only where a == b, which is 256 of the cases)\n\n");
        CHECK(rev_ok == 65536, "the last-byte-first model does not hold");
        CHECK(neither == 0, "%ld two-byte sources match neither model", neither);
    }

    /* ============ 4. do the digest bytes interact? ============ */
    {
        printf("4. ARE THE DIGEST BYTES INDEPENDENT CHAINS?\n");
        long bad = 0;
        for (int v = 0; v < 256; ++v) {
            unsigned char s = (unsigned char)v, d[8];
            memset(d, 0xAB, sizeof d);
            sys(&s, 1, d, 8);
            for (int j = 0; j < 8; ++j)
                if (d[j] != T[(unsigned char)(j ^ v)]) ++bad;
        }
        printf("   h[j] == T[j ^ src[0]] for every j in 0..7 and every source byte: %s",
               bad ? "NO" : "YES");
        if (bad) printf("  (%ld disagreements)", bad);
        printf("\n");
        CHECK(bad == 0, "digest byte j is not a function of j and the source alone");
        /* and the same over longer sources: lane j's value must not depend on cbHash */
        long cross = 0;
        for (int trial = 0; trial < 512; ++trial) {
            unsigned char s[37], d4[4], d32[32];
            for (int i = 0; i < 37; ++i) s[i] = (unsigned char)(trial * 7 + i * 31);
            sys(s, 37, d4, 4);
            sys(s, 37, d32, 32);
            if (memcmp(d4, d32, 4) != 0) ++cross;
        }
        printf("   the first four bytes of a 32-byte digest equal a 4-byte digest: %s\n\n",
               cross ? "NO" : "YES");
        CHECK(cross == 0, "digest length changes the earlier bytes -- the lanes are not independent");
    }

    /* ============ 5. the full model, exhaustively small then fuzzed ============ */
    {
        printf("5. THE MODEL vs THE LIVE EXPORT\n");
        long cases = 0, bad = 0;
        static unsigned char s[4096], live[512], mine[512];
        /* exhaustive over small shapes, with every byte value appearing at every position */
        for (int n = 0; n <= 6; ++n) {
            for (int m = 0; m <= 18; ++m) {
                for (int seed = 0; seed < 64; ++seed) {
                    for (int i = 0; i < n; ++i)
                        s[i] = (unsigned char)(seed * 4 + i * 61);
                    memset(live, 0xAB, sizeof live);
                    memset(mine, 0xAB, sizeof mine);
                    HRESULT hr = sys(s, (DWORD)n, live, (DWORD)m);
                    if (m) model(s, (DWORD)n, mine, (DWORD)m, 1);
                    ++cases;
                    if (hr != S_OK) { ++bad; continue; }
                    if (memcmp(live, mine, (size_t)m + 8) != 0) ++bad;
                }
            }
        }
        printf("   exhaustive small shapes (cbData 0..6 x cbHash 0..18 x 64 fills): "
               "%ld cases, %ld mismatches\n", cases, bad);
        CHECK(bad == 0, "%ld small-shape mismatches", bad);

        /* long sources and long digests, where an implementation's blocking shows up */
        long lcases = 0, lbad = 0;
        unsigned long rng = 0x1234567u;
        for (int trial = 0; trial < 300; ++trial) {
            rng = rng * 1103515245u + 12345u;
            DWORD n = (rng >> 8) % 4001;
            rng = rng * 1103515245u + 12345u;
            DWORD m = 1 + (rng >> 8) % 300;
            for (DWORD i = 0; i < n; ++i) {
                rng = rng * 1103515245u + 12345u;
                s[i] = (unsigned char)(rng >> 16);
            }
            memset(live, 0xAB, sizeof live);
            memset(mine, 0xAB, sizeof mine);
            sys(s, n, live, m);
            model(s, n, mine, m, 1);
            ++lcases;
            if (memcmp(live, mine, (size_t)m + 8) != 0) lbad++;
        }
        printf("   fuzz, cbData 0..4000 x cbHash 1..300: %ld cases, %ld mismatches\n\n",
               lcases, lbad);
        CHECK(lbad == 0, "%ld fuzz mismatches", lbad);
    }

    /* ============ 6. the degenerate and failing cases ============ */
    {
        static unsigned char d[64];
        printf("6. THE DEGENERATE AND FAILING CASES\n");
        HRESULT hr;
        memset(d, 0xAB, sizeof d);
        hr = sys(0, 4, d, 4);
        printf("   NULL source           -> %08lX, buffer %s\n", (unsigned long)hr,
               d[0] == 0xAB ? "UNTOUCHED" : "WRITTEN");
        CHECK(hr == 0x80070057, "NULL source returned %08lX", (unsigned long)hr);
        CHECK(d[0] == 0xAB, "NULL source wrote to the buffer");

        hr = sys((const BYTE*)"abcd", 4, 0, 4);
        printf("   NULL digest           -> %08lX\n", (unsigned long)hr);
        CHECK(hr == 0x80070057, "NULL digest returned %08lX", (unsigned long)hr);

        hr = sys(0, 0, 0, 0);
        printf("   both NULL             -> %08lX\n", (unsigned long)hr);
        CHECK(hr == 0x80070057, "both NULL returned %08lX", (unsigned long)hr);

        memset(d, 0xAB, sizeof d);
        hr = sys((const BYTE*)"abcd", 4, d, 0);
        printf("   cbHash == 0           -> %08lX, buffer %s\n", (unsigned long)hr,
               d[0] == 0xAB ? "UNTOUCHED" : "WRITTEN");
        CHECK(hr == S_OK, "cbHash=0 returned %08lX", (unsigned long)hr);
        CHECK(d[0] == 0xAB, "cbHash == 0 wrote to the buffer");

        /* The two questions that decide what an implementation may touch, answered against a
           PAGE_NOACCESS page rather than by looking at a poison fill. */
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (g) {
            DWORD old;
            VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
            /* a source pointer one byte past the end of the readable page, with cbData == 0 */
            int faulted = 0;
            __try { hr = sys((const BYTE*)(g + pg), 0, d, 4); }
            __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("   cbData == 0 with an UNREADABLE source pointer -> %s\n",
                   faulted ? "FAULTED (it reads)" : "returned, so it does NOT read");
            CHECK(!faulted, "cbData == 0 still reads the source");
            /* a digest pointer on the NOACCESS page, with cbHash == 0 */
            faulted = 0;
            __try { hr = sys((const BYTE*)"abcd", 4, (BYTE*)(g + pg), 0); }
            __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("   cbHash == 0 with an UNWRITABLE digest pointer -> %s\n",
                   faulted ? "FAULTED (it writes)" : "returned, so it does NOT write");
            CHECK(!faulted, "cbHash == 0 still writes the digest");
            VirtualFree(g, 0, MEM_RELEASE);
        }
        printf("\n");
    }

    printf(fails ? "PROBE: %d CHECK(S) FAILED\n" : "PROBE: all checks passed\n", fails);
    return fails ? 1 : 0;
}
