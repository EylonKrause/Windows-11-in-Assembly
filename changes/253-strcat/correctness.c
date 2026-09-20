/* changes/253-strcat/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ucrtbase export.
 *
 * What is compared is not the string. It is every byte of the whole destination buffer, plus the
 * returned pointer. That is the whole point of testing a function that WRITES.
 *
 * A search can be checked by its answer; a copy cannot. `strcat` is specified to write exactly
 * strlen(src)+1 units at dst+strlen(dst) and nothing ELSE, and every plausible vectorised mistake
 *, storing a whole 32-byte block for a three-byte tail, rounding a length up to an alignment
 * boundary, using an overlapping store pair that reaches backwards past the start, produces a
 * perfectly correct string, a perfectly correct return value, and silently destroys whatever the
 * caller had after the destination. probes/contract.c measured that the shipped export disturbs
 * nothing past the terminator even for an empty source; so every buffer here is pre-filled with a
 * canary and compared in full.
 *
 * THE CORPORA:
 *   1. Every alignment x every length, exhaustively. Both pointers are slid across a 32-byte
 *      window independently, because the implementation's first-block handling is driven by
 *      `src & 31` and its store ladder by the tail length, so the interesting cases are the
 *      products of the two, not either alone.
 *   2. Lengths that straddle the block loop, 0..200, which is where "the terminator is the first
 *      byte of a new block" and "the terminator is the last byte of a block" live.
 *   3. A PAGE_NOACCESS GUARD PAGE immediately after the source, which turns an over-read into a
 *      fault. This is the claim that the aligned-block walk needs no clamp, tested.
 *   4. A GUARD PAGE immediately after the destination's last legal byte, which turns an over-WRITE
 *      into a fault rather than a silent canary mismatch, belt and braces against 1.
 *
 * NULL is deliberately absent: probes/contract.c established that the shipped export FAULTS on a
 * NULL in either argument, so there is no contract to match, and ours faults at the same first
 * touch.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

char*    wia_strcat(char*, const char*);
wchar_t* wia_wcscat(wchar_t*, const wchar_t*);
char*    ref_strcat(char*, const char*);
wchar_t* ref_wcscat(wchar_t*, const wchar_t*);

typedef char*    (*FCAT)(char*, const char*);
typedef wchar_t* (*FWCAT)(wchar_t*, const wchar_t*);
static FCAT  live_strcat;
static FWCAT live_wcscat;

static long fails = 0, cases = 0;
#define BUF 4096

static unsigned char bo[BUF], br[BUF], bl[BUF];   /* ours / reference / live */

/* One narrow case. dst_off and src_off are byte offsets into their buffers. */
static void one_a(int dst_off, int dn, int src_off, int sn, const char* where)
{
    static unsigned char srcbuf[BUF];
    char *ro, *rr, *rl;
    int i;
    ++cases;

    memset(bo, 0xC7, BUF); memset(br, 0xC7, BUF); memset(bl, 0xC7, BUF);
    memset(srcbuf, 0xD3, BUF);
    for (i = 0; i < dn; ++i) { bo[dst_off + i] = br[dst_off + i] = bl[dst_off + i] =
                               (unsigned char)('A' + i % 26); }
    bo[dst_off + dn] = br[dst_off + dn] = bl[dst_off + dn] = 0;
    for (i = 0; i < sn; ++i) srcbuf[src_off + i] = (unsigned char)('a' + i % 26);
    srcbuf[src_off + sn] = 0;

    ro = wia_strcat((char*)bo + dst_off, (const char*)srcbuf + src_off);
    rr = ref_strcat((char*)br + dst_off, (const char*)srcbuf + src_off);
    rl = live_strcat((char*)bl + dst_off, (const char*)srcbuf + src_off);

    if (ro != (char*)bo + dst_off || rr != (char*)br + dst_off || rl != (char*)bl + dst_off) {
        if (++fails <= 20) printf("  FAIL [%s] return != dst  (dn=%d sn=%d da=%d sa=%d)\n",
                                  where, dn, sn, dst_off & 31, src_off & 31);
        return;
    }
    /* The whole buffer, not the string */
    if (memcmp(bo, br, BUF) != 0 || memcmp(bo, bl, BUF) != 0) {
        if (++fails <= 20) {
            int k;
            for (k = 0; k < BUF; ++k) if (bo[k] != br[k] || bo[k] != bl[k]) break;
            printf("  FAIL [%s] buffer differs at byte %d (dst starts %d, ends %d; "
                   "dn=%d sn=%d da=%d sa=%d)  ours=%02X ref=%02X live=%02X\n",
                   where, k, dst_off, dst_off + dn + sn, dn, sn, dst_off & 31, src_off & 31,
                   bo[k], br[k], bl[k]);
            if (k > dst_off + dn + sn)
                printf("        ^ that byte is PAST the new terminator: a write that should "
                       "never have happened\n");
        }
    }
}

/* One wide case. dst_off and src_off are CHARACTER offsets. */
static void one_w(int dst_off, int dn, int src_off, int sn, const char* where)
{
    static wchar_t sw[BUF / 2];
    wchar_t *wo = (wchar_t*)bo, *wr = (wchar_t*)br, *wl = (wchar_t*)bl;
    wchar_t *ro, *rr, *rl;
    int i, n = BUF / 2;
    ++cases;

    memset(bo, 0xC7, BUF); memset(br, 0xC7, BUF); memset(bl, 0xC7, BUF);
    for (i = 0; i < n; ++i) sw[i] = 0xD3D3;
    for (i = 0; i < dn; ++i) { wo[dst_off + i] = wr[dst_off + i] = wl[dst_off + i] =
                               (wchar_t)(0x0410 + i % 32); }
    wo[dst_off + dn] = wr[dst_off + dn] = wl[dst_off + dn] = 0;
    for (i = 0; i < sn; ++i) sw[src_off + i] = (wchar_t)(0x0430 + i % 32);
    sw[src_off + sn] = 0;

    ro = wia_wcscat(wo + dst_off, sw + src_off);
    rr = ref_wcscat(wr + dst_off, sw + src_off);
    rl = live_wcscat(wl + dst_off, sw + src_off);

    if (ro != wo + dst_off || rr != wr + dst_off || rl != wl + dst_off) {
        if (++fails <= 20) printf("  FAIL [%s] wide return != dst (dn=%d sn=%d)\n", where, dn, sn);
        return;
    }
    if (memcmp(bo, br, BUF) != 0 || memcmp(bo, bl, BUF) != 0) {
        if (++fails <= 20) {
            int k;
            for (k = 0; k < BUF; ++k) if (bo[k] != br[k] || bo[k] != bl[k]) break;
            printf("  FAIL [%s] wide buffer differs at byte %d (dn=%d sn=%d da=%d sa=%d) "
                   "ours=%02X ref=%02X live=%02X\n",
                   where, k, dn, sn, (dst_off * 2) & 31, (src_off * 2) & 31, bo[k], br[k], bl[k]);
        }
    }
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ucrtbase.dll");
    if (!h) h = LoadLibraryW(L"ucrtbase.dll");
    live_strcat = (FCAT)GetProcAddress(h, "strcat");
    live_wcscat = (FWCAT)GetProcAddress(h, "wcscat");
    if (!live_strcat || !live_wcscat) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: strcat / wcscat (ours vs oracle vs LIVE ucrtbase) ==\n");
    printf("   every case compares the ENTIRE %d-byte destination buffer, not the string\n", BUF);

    /* ---- 1. every alignment x every alignment, over short lengths ---- */
    {
        long before = cases;
        int da, sa, dn, sn;
        for (da = 0; da < 32; ++da)
            for (sa = 0; sa < 32; ++sa)
                for (dn = 0; dn <= 6; ++dn)
                    for (sn = 0; sn <= 40; ++sn)
                        one_a(64 + da, dn, 64 + sa, sn, "align x align");
        printf("  1. narrow: dst align 0..31 x src align 0..31 x dst len 0..6 x src len 0..40: "
               "%ld cases\n", cases - before);
    }

    /* ---- 2. lengths that straddle the block loop ---- */
    {
        long before = cases;
        int dn, sn, sa;
        for (sa = 0; sa < 32; sa += 7)
            for (dn = 0; dn <= 200; dn += 7)
                for (sn = 0; sn <= 200; ++sn)
                    one_a(64, dn, 64 + sa, sn, "block straddle");
        printf("  2. narrow: src len 0..200 across the 32-byte block boundary, 5 src alignments, "
               "29 dst lengths: %ld cases\n", cases - before);
    }

    /* ---- 3. the same two, wide ---- */
    {
        long before = cases;
        int da, sa, dn, sn;
        for (da = 0; da < 16; ++da)
            for (sa = 0; sa < 16; ++sa)
                for (dn = 0; dn <= 4; ++dn)
                    for (sn = 0; sn <= 24; ++sn)
                        one_w(32 + da, dn, 32 + sa, sn, "wide align x align");
        for (sa = 0; sa < 16; sa += 3)
            for (dn = 0; dn <= 100; dn += 7)
                for (sn = 0; sn <= 100; ++sn)
                    one_w(32, dn, 32 + sa, sn, "wide block straddle");
        printf("  3. wide: alignments and block-straddling lengths: %ld cases\n", cases - before);
    }

    /* ---- 4. a guard page after the source: an over-read must fault ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        int sn, da;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  4. guard page SKIPPED (allocation failed)\n");
        } else {
            static unsigned char d1[BUF], d2[BUF];
            for (sn = 1; sn <= 300; ++sn) {
                /* the source's terminator is the LAST byte before PAGE_NOACCESS */
                char* s = (base + si.dwPageSize) - (sn + 1);
                int i;
                for (i = 0; i < sn; ++i) s[i] = (char)('a' + i % 26);
                s[sn] = 0;
                for (da = 0; da < 32; da += 5) {
                    char *ra, *rb;
                    memset(d1, 0xC7, BUF); memset(d2, 0xC7, BUF);
                    d1[64 + da] = d2[64 + da] = 0;
                    ra = wia_strcat((char*)d1 + 64 + da, s);
                    rb = ref_strcat((char*)d2 + 64 + da, s);
                    ++cases; ++guard;
                    if (ra != (char*)d1 + 64 + da || rb != (char*)d2 + 64 + da ||
                        memcmp(d1, d2, BUF) != 0) {
                        if (++fails <= 20) printf("  FAIL [guard/src] sn=%d da=%d\n", sn, da);
                    }
                }
            }
            printf("  4. guard page immediately after the SOURCE, src len 1..300 x 7 dst "
                   "alignments: %ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* ---- 5. a guard page after the destination: an over-WRITE must fault ---- */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        int dn, sn;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  5. guard page SKIPPED (allocation failed)\n");
        } else {
            static char srcs[512];
            int i;
            for (i = 0; i < 400; ++i) srcs[i] = (char)('a' + i % 26);
            for (dn = 0; dn <= 40; ++dn)
                for (sn = 0; sn <= 200; ++sn) {
                    /* dst is placed so that dst + dn + sn + 1 lands exactly on the guard page:
                       one byte of over-write raises instead of merely corrupting */
                    char* d = (base + si.dwPageSize) - (dn + sn + 1);
                    char* r;
                    srcs[sn] = 0;
                    for (i = 0; i < dn; ++i) d[i] = (char)('A' + i % 26);
                    d[dn] = 0;
                    r = wia_strcat(d, srcs);
                    ++cases; ++guard;
                    if (r != d || (int)strlen(d) != dn + sn) {
                        if (++fails <= 20) printf("  FAIL [guard/dst] dn=%d sn=%d len=%d\n",
                                                  dn, sn, (int)strlen(d));
                    }
                    srcs[sn] = (char)('a' + sn % 26);
                }
            printf("  5. guard page at the DESTINATION's last legal byte, dst 0..40 x src 0..200: "
                   "%ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    printf("\n  total cases: %ld,  failures: %ld\n", cases, fails);
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (whole-buffer exact vs oracle AND live)\n");
    return fails ? 1 : 0;
}
