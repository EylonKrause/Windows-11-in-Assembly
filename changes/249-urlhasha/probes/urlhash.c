/* changes/249-urlhasha/probes/urlhash.c
 *
 * The contract of shlwapi/kernelbase!UrlHashA, measured against the live export.
 *
 * Why this one, and why it is small. The disassembly settles most of it before a probe runs.
 * kernelbase!UrlHashA (rva 0x12F750) is twenty-two instructions:
 *
 *     0012F768  test rcx, rcx / je    pszUrl  NULL -> 0x80070057
 *     0012F76D  test rdx, rdx / je    pbHash  NULL -> 0x80070057
 *     0012F772  call 0x4C150          = lstrlenA -- the one with an SEH HANDLER
 *     0012F782  call 0x0C0A10         the hash worker, (pszUrl, len, pbHash, cbHash)
 *     0012F787  xor eax, eax          S_OK, UNCONDITIONALLY -- the worker's result is discarded
 *
 * and kernelbase!UrlHashW (rva 0x12F7B0) is a wide-to-narrow converter that then calls UrlHashA:
 * a 65-byte inline string builder at [rsp+0x20] with its capacity 0x41 written at [rsp+0x70], the
 * conversion at 0x4AF18, and then `call 0x12F750` -- which IS UrlHashA. So there is one hash in this
 * pair, not two, and a patch on the narrow export is a patch on both.
 *
 * What the worker at 0xC0A10 is. discovery/README.md records it as "byte-identical to the HashData
 * export at 0xBB750 down to the same permutation table", and that is very slightly overstated. The
 * two instruction streams were diffed for this change: the worker is the export's body MINUS the
 * export's own two NULL checks and MINUS its trailing `xor eax, eax` -- it returns nothing, and its
 * caller supplies the S_OK. Everything else matches instruction for instruction, and both reach the
 * SAME permutation table: `lea rsi,[rip+0x1E55C4]` at 0x0C0A45 and `lea rsi,[rip+0x1EA874]` at
 * 0x0BB795 both resolve to RVA 0x2A6010, which is the table change 244 reproduced as c_tab.
 *
 * So UrlHashA should be exactly:  lstrlenA, then HashData's body, then S_OK -- which makes this
 * change a COMPOSITION of two landed ones (225 for the length INCLUDING its fault swallow, 244 for
 * the hash) over a six-instruction envelope. That is a claim to TEST rather than assume, and it is
 * what sections 3 and 4 below are for: if UrlHashA's digest is not bit-identical to HashData's on
 * the same bytes, the composition is wrong and the change does not exist.
 *
 * The four things only a probe can settle:
 *   1. cbHash = 0, and cbHash larger than any digest anyone would ask for -- the disassembly does
 *      not validate cbHash at all, so whatever the worker does with it IS the contract.
 *   2. a faulting url. lstrlenA is SEH-wrapped; the survey recorded that an unterminated url at a
 *      PAGE_NOACCESS boundary returns S_OK with the identity seed. Asked directly here.
 *   3. OVERLAP of pszUrl and pbHash. Change 244 measured that the shipped hash loop re-reads the
 *      source byte for every digest lane, so a digest write landing on the source changes what the
 *      remaining lanes consume -- wrong on all 1641 overlapping placements of a grouped
 *      implementation. UrlHashA hands the caller's own pointers straight through, so that hazard
 *      arrives here too.
 *   4. That UrlHashW really is UrlHashA. Same digest for the same ASCII text, and the same answer
 *      for the arguments the narrow one refuses.
 *
 * Read-only with respect to the system: nothing is patched, nothing is written to disk.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *FA)(const char*, BYTE*, DWORD);
typedef HRESULT (WINAPI *FW)(const wchar_t*, BYTE*, DWORD);
typedef HRESULT (WINAPI *FH)(const BYTE*, DWORD, BYTE*, DWORD);
static FA ha;
static FW hw;
static FH hd;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 40) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define SENT 0xAB

static void show(const BYTE* h, DWORD n)
{
    DWORD i;
    for (i = 0; i < n && i < 24; ++i) printf("%02X", h[i]);
    if (n > 24) printf("..");
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    HMODULE k = LoadLibraryW(L"kernelbase.dll");
    ha = (FA)GetProcAddress(h, "UrlHashA");
    hw = (FW)GetProcAddress(h, "UrlHashW");
    hd = (FH)GetProcAddress(h, "HashData");
    if (!ha || !hw || !hd) { printf("cannot resolve UrlHashA/W or HashData\n"); return 1; }
    printf("UrlHashA contract probe  (GetACP() = %u)\n", GetACP());
    printf("  shlwapi!UrlHashA  %p   kernelbase!UrlHashA  %p\n",
           (void*)ha, (void*)GetProcAddress(k, "UrlHashA"));
    printf("  shlwapi!UrlHashW  %p   kernelbase!UrlHashW  %p\n\n",
           (void*)hw, (void*)GetProcAddress(k, "UrlHashW"));

    static BYTE g1[512], g2[512];
    HRESULT hr;

    /* ============ 1. the two NULL checks, and whether cbHash is validated at all ============ */
    {
        printf("1. NULL ARGUMENTS, AND cbHash\n");
        memset(g1, SENT, sizeof g1);
        hr = ha(0, g1, 16);
        printf("   pszUrl NULL -> %08lX, digest %s\n", (unsigned long)hr,
               g1[0] == SENT ? "untouched" : "WRITTEN");
        CHECK(hr == 0x80070057, "NULL url returned %08lX", (unsigned long)hr);
        CHECK(g1[0] == SENT, "NULL url wrote to the digest");
        hr = ha("abc", 0, 16);
        printf("   pbHash NULL -> %08lX\n", (unsigned long)hr);
        CHECK(hr == 0x80070057, "NULL hash returned %08lX", (unsigned long)hr);

        memset(g1, SENT, sizeof g1);
        hr = ha("abc", g1, 0);
        printf("   cbHash 0    -> %08lX, first byte %02X %s\n", (unsigned long)hr, g1[0],
               g1[0] == SENT ? "(untouched)" : "");
        CHECK(hr == S_OK, "cbHash 0 returned %08lX", (unsigned long)hr);
        CHECK(g1[0] == SENT, "cbHash 0 wrote a byte");

        memset(g1, SENT, sizeof g1);
        hr = ha("", g1, 8);
        printf("   empty url, cbHash 8 -> %08lX, digest ", (unsigned long)hr);
        show(g1, 8);
        printf("   (the identity seed, if the URL contributes nothing)\n");
        CHECK(hr == S_OK, "empty url returned %08lX", (unsigned long)hr);
        {
            int seed = 1, i;
            for (i = 0; i < 8; ++i) if (g1[i] != (BYTE)i) seed = 0;
            CHECK(seed, "an empty url did not leave the identity seed");
        }
        printf("\n");
    }

    /* ============ 2. is it S_OK for every cbHash? ============ */
    {
        DWORD bad = 0, cb;
        printf("2. EVERY cbHash FROM 0 TO 256 -- the disassembly validates none of them\n");
        for (cb = 0; cb <= 256; ++cb) {
            memset(g1, SENT, sizeof g1);
            hr = ha("http://example.com/a/b?c=d", g1, cb);
            if (hr != S_OK) ++bad;
            /* nothing past cbHash may be written */
            if (g1[cb] != SENT) { CHECK(0, "cbHash=%lu wrote past the digest", (unsigned long)cb); }
        }
        printf("   %lu of 257 returned something other than S_OK\n\n", (unsigned long)bad);
        CHECK(bad == 0, "%lu cbHash values did not return S_OK", (unsigned long)bad);
    }

    /* ============ 3. The composition claim: UrlHashA == lstrlenA + HashData ============ */
    {
        static const char* U[] = {
            "", "a", "ab", "abc", "http://example.com/", "HTTP://EXAMPLE.COM/",
            "http://example.com/a/b/c?d=e&f=g#h", "\x01\x02\x03\xFF\xFE",
            "a very long url that will not fit in any inline buffer anyone would have chosen "
            "for it, and which therefore exercises whatever the worker does with a long source",
        };
        long bad = 0, n = 0;
        DWORD cb;
        printf("3. UrlHashA(url, h, cb)  ==  HashData(url, strlen(url), h, cb)?\n"
               "   This is the whole change: if it holds, 249 is change 244's kernel behind a\n"
               "   six-instruction envelope and change 225's length scan.\n");
        for (int i = 0; i < (int)(sizeof U / sizeof U[0]); ++i)
            for (cb = 0; cb <= 40; ++cb) {
                memset(g1, SENT, sizeof g1);
                memset(g2, SENT, sizeof g2);
                HRESULT r1 = ha(U[i], g1, cb);
                HRESULT r2 = hd((const BYTE*)U[i], (DWORD)strlen(U[i]), g2, cb);
                ++n;
                if (r1 != S_OK || r2 != S_OK || memcmp(g1, g2, sizeof g1) != 0) {
                    ++bad;
                    if (bad <= 4) {
                        printf("   DIFFER at cb=%lu url=\"%.40s\"\n     UrlHashA ", (unsigned long)cb, U[i]);
                        show(g1, cb ? cb : 4);
                        printf("  (%08lX)\n     HashData ", (unsigned long)r1);
                        show(g2, cb ? cb : 4);
                        printf("  (%08lX)\n", (unsigned long)r2);
                    }
                }
            }
        printf("   %ld cases, %ld disagreements\n\n", n, bad);
        CHECK(bad == 0, "%ld of %ld disagree with HashData", bad, n);
    }

    /* ============ 4. UrlHashW: is it really UrlHashA on the converted text? ============ */
    {
        long bad = 0, n = 0;
        printf("4. UrlHashW(L\"x\") == UrlHashA(\"x\") for ASCII -- the wide export CALLS the\n"
               "   narrow one (0x12F81F: call 0x12F750), so one patch would cover both\n");
        {
            static const char* U[] = { "", "a", "abc", "http://example.com/a?b=c",
                                       "0123456789012345678901234567890123456789"
                                       "0123456789012345678901234567890123456789" };
            for (int i = 0; i < 5; ++i) {
                wchar_t w[256];
                DWORD cb;
                int j;
                for (j = 0; U[i][j]; ++j) w[j] = (wchar_t)(unsigned char)U[i][j];
                w[j] = 0;
                for (cb = 0; cb <= 32; ++cb) {
                    memset(g1, SENT, sizeof g1);
                    memset(g2, SENT, sizeof g2);
                    HRESULT r1 = ha(U[i], g1, cb);
                    HRESULT r2 = hw(w, g2, cb);
                    ++n;
                    if (r1 != r2 || memcmp(g1, g2, sizeof g1) != 0) ++bad;
                }
            }
        }
        printf("   %ld cases, %ld disagreements\n", n, bad);
        CHECK(bad == 0, "%ld of %ld wide/narrow pairs disagree", bad, n);
        memset(g1, SENT, sizeof g1);
        printf("   UrlHashW(NULL) -> %08lX,  UrlHashW(L\"a\", NULL) -> %08lX\n\n",
               (unsigned long)hw(0, g1, 16), (unsigned long)hw(L"a", 0, 16));
    }

    /* ============ 5. OVERLAP of the URL and the digest, inside one buffer ============ */
    {
        /* Change 244 established that the shipped hash loop RE-READS the source byte for every
           digest lane, so a digest write that lands on the source changes what the remaining lanes
           of that same source byte consume -- its grouped kernel is wrong on all 1641 overlapping
           placements and right on all 760 disjoint ones. UrlHashA passes the caller's pointers
           straight to that loop, so the same hazard is reachable through this export. */
        static BYTE buf[256];
        int doff, bad = 0, n = 0;
        printf("5. OVERLAP: a 16-byte url and a 12-byte digest at every relative placement\n");
        for (doff = -20; doff <= 20; ++doff) {
            BYTE ref[64];
            int i;
            int ub = 64;
            if (ub + doff < 0) continue;
            memset(buf, SENT, sizeof buf);
            for (i = 0; i < 16; ++i) buf[ub + i] = (BYTE)('a' + i);
            buf[ub + 16] = 0;
            ha((const char*)(buf + ub), buf + ub + doff, 12);
            memcpy(ref, buf + ub + doff, 12);
            /* re-run the same placement to prove it is deterministic, not a torn read */
            memset(buf, SENT, sizeof buf);
            for (i = 0; i < 16; ++i) buf[ub + i] = (BYTE)('a' + i);
            buf[ub + 16] = 0;
            ha((const char*)(buf + ub), buf + ub + doff, 12);
            if (memcmp(ref, buf + ub + doff, 12) != 0) ++bad;
            ++n;
        }
        printf("   %d placements, %d non-deterministic  (all of them are well defined and must be\n"
               "   reproduced exactly, overlapping or not)\n\n", n, bad);
        CHECK(bad == 0, "%d overlapping placements were not reproducible", bad);
    }

    /* ============ 6. The faulting url -- does lstrlenA's swallow show through? ============ */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        {
            SIZE_T pg = si.dwPageSize;
            char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            DWORD old;
            printf("6. A FAULTING URL at a PAGE_NOACCESS page. lstrlenA has an SEH handler and\n"
                   "   returns 0, so this should hash NOTHING and leave the identity seed --\n"
                   "   never fault. An implementation without a __try would crash here.\n");
            if (g) {
                int tail;
                VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
                for (tail = 1; tail <= 8; ++tail) {
                    char* s = (g + pg) - tail;
                    int faulted = 0, i, seed = 1;
                    for (i = 0; i < tail; ++i) s[i] = (char)('a' + i);   /* NO terminator */
                    memset(g1, SENT, sizeof g1);
                    __try { hr = ha(s, g1, 8); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
                    printf("   unterminated %d-byte url -> %s", tail, faulted ? "FAULTED" : "");
                    if (!faulted) {
                        printf("%08lX, digest ", (unsigned long)hr);
                        show(g1, 8);
                        for (i = 0; i < 8; ++i) if (g1[i] != (BYTE)i) seed = 0;
                        printf("  %s", seed ? "(the identity seed)" : "(NOT the seed)");
                    }
                    printf("\n");
                    CHECK(!faulted, "an unterminated url FAULTED (tail=%d); lstrlenA swallows it",
                          tail);
                    CHECK(faulted || hr == S_OK, "an unterminated url returned %08lX",
                          (unsigned long)hr);
                }
                /* and a url TERMINATED at the last readable byte must not fault either */
                for (tail = 2; tail <= 40; ++tail) {
                    char* s = (g + pg) - tail;
                    int faulted = 0, i;
                    for (i = 0; i < tail - 1; ++i) s[i] = (char)('a' + i % 26);
                    s[tail - 1] = 0;
                    memset(g1, SENT, sizeof g1);
                    __try { hr = ha(s, g1, 8); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
                    CHECK(!faulted, "a url terminated at the last readable byte faulted (tail=%d)",
                          tail);
                }
                printf("   a url TERMINATED at the last readable byte: never faults, 39 lengths\n");
                VirtualFree(g, 0, MEM_RELEASE);
            }
            printf("\n");
        }
    }

    /* ============ 7. is the digest a pure function of the BYTES, with no locale? ============ */
    {
        /* Change 244 established HashData has no code page anywhere in it. UrlHashA adds only
           lstrlenA, which has none either -- so every byte value 1..255 must go straight through.
           If any byte were folded, this table would show two inputs sharing a digest. */
        int i, collisions = 0;
        static BYTE d[256][4];
        printf("7. ALL 255 NON-NUL SINGLE-BYTE URLS -- a code-page fold would collide two of them\n");
        for (i = 1; i < 256; ++i) {
            char s[2]; s[0] = (char)i; s[1] = 0;
            ha(s, d[i], 4);
        }
        for (i = 1; i < 256; ++i) {
            int j;
            for (j = i + 1; j < 256; ++j) if (memcmp(d[i], d[j], 4) == 0) ++collisions;
        }
        printf("   %d colliding pairs among 255 one-byte urls\n\n", collisions);
        CHECK(collisions == 0, "%d one-byte urls collide -- something folds bytes", collisions);
    }

    printf(fails ? "PROBE: %d CHECK(S) FAILED\n" : "PROBE: all checks passed\n", fails);
    return fails ? 1 : 0;
}
