/* changes/296-rtlcopyunicodestring/correctness.c: the gate.
 *
 * Compares wia_copyus against reference.c AND against the LIVE ntdll!RtlCopyUnicodeString
 * resolved with GetProcAddress. A single mismatch fails.
 *
 * What "every output byte" means here. The function returns void, so there is no return value to
 * compare and every observable effect is in the destination: the UNICODE_STRING fields and the
 * bytes behind dst->Buffer. Each case therefore runs the three implementations against THREE
 * IDENTICAL 16 KB ARENAS and compares the whole arena byte for byte, not just the destination
 * window. That catches:
 *
 *   * a write past the logical end (the arena beyond MaximumLength must be untouched),
 *   * a write before the destination,
 *   * any damage to the source, which lives in the same arena,
 *   * dst->MaximumLength being modified (compared explicitly),
 *
 * and, because the source and destination are placed in the same arena at a caller-chosen delta,
 * the whole OVERLAP contract falls out of the same comparison. probes/contract.c proved the
 * shipped code is a real memmove (it matches C memmove byte for byte at n = 512, dst = src + 8),
 * so overlap is IN contract and is swept here at every delta from -80 to +80.
 *
 * A separate pass puts the destination (and then the source) ending exactly at a page boundary
 * with the following page PAGE_NOACCESS, for every length 0..200. A vector load one byte wider
 * than the caller's buffer faults there instead of passing silently.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
typedef struct { USHORT Length, MaximumLength; unsigned short* Buffer; } REF_US;

extern void wia_copyus(US* dst, const US* src);
void ref_copyus(REF_US* dst, const REF_US* src);

typedef void (NTAPI *fn)(US*, const US*);
static fn sys;

static long long checks = 0, failures = 0;

/* ------------------------------------------------------------------ arena machinery -------- */
#define ARENA 16384
static unsigned char A_live[ARENA], A_ours[ARENA], A_ref[ARENA], A_snap[ARENA];

static void fill3(void)
{
    for (int i = 0; i < ARENA; ++i) {
        unsigned char v = (unsigned char)(0x23u + (unsigned)i * 37u + ((unsigned)i >> 5));
        A_live[i] = v; A_ours[i] = v; A_ref[i] = v;
    }
    memcpy(A_snap, A_ours, ARENA);
}

static void report(const char* what, int dstoff, int srcoff, unsigned maxlen, unsigned srclen,
                   US* dl, US* du, REF_US* dr)
{
    if (failures < 25) {
        printf("FAIL [%s] dstoff=%d srcoff=%d max=%u srclen=%u\n", what, dstoff, srcoff, maxlen, srclen);
        printf("    Length      live=%u ours=%u ref=%u\n", dl->Length, du->Length, dr->Length);
        printf("    MaximumLen  live=%u ours=%u ref=%u\n", dl->MaximumLength, du->MaximumLength, dr->MaximumLength);
        for (int i = 0; i < ARENA; ++i) {
            if (A_live[i] != A_ours[i] || A_live[i] != A_ref[i]) {
                printf("    arena@%d live=%02X ours=%02X ref=%02X  (dst window [%d,%d))\n",
                       i, A_live[i], A_ours[i], A_ref[i], dstoff, dstoff + (int)maxlen);
                break;
            }
        }
    }
    ++failures;
}

/* One case: source and destination both inside the arena, at caller-chosen offsets. */
static void run(const char* what, int dstoff, unsigned maxlen, unsigned srclen, int srcoff,
                unsigned dstLenIn)
{
    if (dstoff < 0 || dstoff + (int)maxlen > ARENA) return;
    if (srcoff < 0 || srcoff + (int)srclen > ARENA) return;

    fill3();
    US     dl = { (USHORT)dstLenIn, (USHORT)maxlen, (wchar_t*)(A_live + dstoff) };
    US     sl = { (USHORT)srclen, 0x7777, (wchar_t*)(A_live + srcoff) };
    US     du = { (USHORT)dstLenIn, (USHORT)maxlen, (wchar_t*)(A_ours + dstoff) };
    US     su = { (USHORT)srclen, 0x7777, (wchar_t*)(A_ours + srcoff) };
    REF_US dr = { (USHORT)dstLenIn, (USHORT)maxlen, (unsigned short*)(A_ref + dstoff) };
    REF_US sr = { (USHORT)srclen, 0x7777, (unsigned short*)(A_ref + srcoff) };

    sys(&dl, &sl);
    wia_copyus(&du, &su);
    ref_copyus(&dr, &sr);
    ++checks;

    if (dl.Length != du.Length || dl.Length != dr.Length ||
        dl.MaximumLength != du.MaximumLength || dl.MaximumLength != dr.MaximumLength ||
        memcmp(A_live, A_ours, ARENA) != 0 || memcmp(A_live, A_ref, ARENA) != 0) {
        report(what, dstoff, srcoff, maxlen, srclen, &dl, &du, &dr);
        return;
    }
    /* Independent of the 3-way compare: OUR implementation must not have touched a byte outside
       the destination window the caller declared. */
    for (int i = 0; i < ARENA; ++i) {
        if (i >= dstoff && i < dstoff + (int)maxlen) continue;
        if (A_ours[i] != A_snap[i]) {
            if (failures < 25)
                printf("FAIL [%s] WROTE OUTSIDE [%d,%d): arena@%d %02X -> %02X (max=%u srclen=%u)\n",
                       what, dstoff, dstoff + (int)maxlen, i, A_snap[i], A_ours[i], maxlen, srclen);
            ++failures;
            return;
        }
    }
}

/* ------------------------------------------------------------------ page guard ------------- */
static unsigned char* g_base;
static DWORD g_pagesize;

static void guard_reset(unsigned char* p, int n)
{
    for (int i = 0; i < n; ++i) p[i] = (unsigned char)(0x23u + (unsigned)i * 37u);
}

/* dst ends exactly at the guard page. */
static void guard_dst(unsigned maxlen, unsigned srclen)
{
    static unsigned char srcbuf[4096], out_live[512], out_ours[512], out_ref[512];
    unsigned char* p = g_base + g_pagesize - maxlen;
    for (unsigned i = 0; i < srclen; ++i) srcbuf[i] = (unsigned char)(0x80u + i);

    US dl = { 0, (USHORT)maxlen, (wchar_t*)p }, sl = { (USHORT)srclen, 0x7777, (wchar_t*)srcbuf };
    guard_reset(p, (int)maxlen); sys(&dl, &sl);          memcpy(out_live, p, maxlen);
    US du = { 0, (USHORT)maxlen, (wchar_t*)p }, su = { (USHORT)srclen, 0x7777, (wchar_t*)srcbuf };
    guard_reset(p, (int)maxlen); wia_copyus(&du, &su);   memcpy(out_ours, p, maxlen);
    REF_US dr = { 0, (USHORT)maxlen, (unsigned short*)p };
    REF_US sr = { (USHORT)srclen, 0x7777, (unsigned short*)srcbuf };
    guard_reset(p, (int)maxlen); ref_copyus(&dr, &sr);   memcpy(out_ref, p, maxlen);

    ++checks;
    if (dl.Length != du.Length || dl.Length != dr.Length ||
        memcmp(out_live, out_ours, maxlen) != 0 || memcmp(out_live, out_ref, maxlen) != 0) {
        if (failures < 25)
            printf("FAIL [guard-dst] max=%u srclen=%u: L live=%u ours=%u ref=%u\n",
                   maxlen, srclen, dl.Length, du.Length, dr.Length);
        ++failures;
    }
}

/* src ends exactly at the guard page. */
static void guard_src(unsigned srclen, unsigned maxlen)
{
    static unsigned char dbuf_l[4096], dbuf_o[4096], dbuf_r[4096];
    unsigned char* p = g_base + g_pagesize - srclen;
    guard_reset(p, (int)srclen);
    for (unsigned i = 0; i < sizeof dbuf_l; ++i) { dbuf_l[i] = dbuf_o[i] = dbuf_r[i] = 0x5A; }

    US dl = { 0, (USHORT)maxlen, (wchar_t*)dbuf_l }, sl = { (USHORT)srclen, 0x7777, (wchar_t*)p };
    sys(&dl, &sl);
    US du = { 0, (USHORT)maxlen, (wchar_t*)dbuf_o }, su = { (USHORT)srclen, 0x7777, (wchar_t*)p };
    wia_copyus(&du, &su);
    REF_US dr = { 0, (USHORT)maxlen, (unsigned short*)dbuf_r };
    REF_US sr = { (USHORT)srclen, 0x7777, (unsigned short*)p };
    ref_copyus(&dr, &sr);

    ++checks;
    if (dl.Length != du.Length || dl.Length != dr.Length ||
        memcmp(dbuf_l, dbuf_o, sizeof dbuf_l) != 0 || memcmp(dbuf_l, dbuf_r, sizeof dbuf_l) != 0) {
        if (failures < 25)
            printf("FAIL [guard-src] srclen=%u max=%u: L live=%u ours=%u ref=%u\n",
                   srclen, maxlen, dl.Length, du.Length, dr.Length);
        ++failures;
    }
}

/* ------------------------------------------------------------------ rng ------------------- */
static unsigned long long rs;
static unsigned rng(void) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL;
                            return (unsigned)(rs >> 33); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    sys = (fn)GetProcAddress(h, "RtlCopyUnicodeString");
    if (!sys) { printf("no ntdll!RtlCopyUnicodeString\n"); return 2; }

    /* ---- 1. src == NULL: Length zeroed, buffer and MaximumLength untouched ---------------- */
    for (unsigned maxlen = 0; maxlen <= 64; ++maxlen) {
        for (unsigned inlen = 0; inlen <= 8; inlen += 4) {
            fill3();
            US dl = { (USHORT)inlen, (USHORT)maxlen, (wchar_t*)(A_live + 128) };
            US du = { (USHORT)inlen, (USHORT)maxlen, (wchar_t*)(A_ours + 128) };
            REF_US dr = { (USHORT)inlen, (USHORT)maxlen, (unsigned short*)(A_ref + 128) };
            sys(&dl, NULL); wia_copyus(&du, NULL); ref_copyus(&dr, NULL);
            ++checks;
            if (dl.Length | du.Length | dr.Length ||
                dl.MaximumLength != maxlen || du.MaximumLength != maxlen || dr.MaximumLength != maxlen ||
                memcmp(A_live, A_ours, ARENA) || memcmp(A_live, A_ref, ARENA) ||
                memcmp(A_ours, A_snap, ARENA)) {
                printf("FAIL [src=NULL] max=%u inlen=%u\n", maxlen, inlen); ++failures;
            }
        }
    }

    /* ---- 2. MaximumLength == 0 with Buffer == NULL (both source shapes) ------------------- */
    {
        static const USHORT sl_[] = { 0, 1, 2, 8, 64, 4000 };
        for (int i = 0; i < 6; ++i) {
            US dl = { 0x1234, 0, NULL }, du = { 0x1234, 0, NULL };
            REF_US dr = { 0x1234, 0, NULL };
            US sl = { sl_[i], 0x7777, (wchar_t*)A_live };
            US su = { sl_[i], 0x7777, (wchar_t*)A_ours };
            REF_US sr = { sl_[i], 0x7777, (unsigned short*)A_ref };
            sys(&dl, &sl); wia_copyus(&du, &su); ref_copyus(&dr, &sr);
            ++checks;
            if (dl.Length || du.Length || dr.Length || dl.MaximumLength || du.MaximumLength || dr.MaximumLength) {
                printf("FAIL [null-buffer max=0] srclen=%u\n", sl_[i]); ++failures;
            }
        }
    }

    /* ---- 3. every length 0..200 (>= 6x the 32-byte vector width), every interesting
              MaximumLength around it, at aligned and unaligned starts ----------------------- */
    {
        static const int dstoffs[] = { 0, 1, 2, 3, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65 };
        for (unsigned srclen = 0; srclen <= 200; ++srclen) {
            unsigned maxes[10];
            int nm = 0;
            maxes[nm++] = 0;
            maxes[nm++] = 1;
            if (srclen >= 2) maxes[nm++] = srclen - 2;
            if (srclen >= 1) maxes[nm++] = srclen - 1;
            maxes[nm++] = srclen;
            maxes[nm++] = srclen + 1;
            maxes[nm++] = srclen + 2;
            maxes[nm++] = srclen + 3;
            maxes[nm++] = srclen + 33;
            maxes[nm++] = 255;
            for (int oi = 0; oi < (int)(sizeof dstoffs / sizeof dstoffs[0]); ++oi) {
                for (int mi = 0; mi < nm; ++mi)
                    run("sweep", 1024 + dstoffs[oi], maxes[mi], srclen, 4096 + (dstoffs[oi] & 7), 0xBEEF);
            }
        }
    }

    /* ---- 4. OVERLAP sweep: destination at every delta from -80..+80 relative to the source,
              including delta 0 (dst->Buffer == src->Buffer) -------------------------------- */
    {
        /* The lengths past 1000 are here because the implementation switches to `rep movsb` at a
           size threshold, and a forward byte copy is only correct with the destination BELOW the
           source. The negative deltas at 1023..4096 are the cases that catch a head block stored
           before the copy has read it -- which is exactly how the first ERMS draft was wrong. */
        static const unsigned ns[] = { 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 47, 63, 64, 65,
                                       79, 80, 96, 127, 128, 129, 191, 255, 256, 257, 512, 1000,
                                       1023, 1024, 1025, 2047, 2048, 2049, 4095, 4096 };
        for (int k = 0; k < (int)(sizeof ns / sizeof ns[0]); ++k) {
            for (int delta = -80; delta <= 80; ++delta) {
                run("overlap", 4096 + delta, ns[k] + 8, ns[k], 4096, 0);
                run("overlap-trunc", 4096 + delta, ns[k] - (ns[k] ? 1 : 0), ns[k], 4096, 0);
            }
        }
    }

    /* ---- 5. self-copy: the SAME UNICODE_STRING as both arguments -------------------------- */
    for (unsigned srclen = 0; srclen <= 128; ++srclen) {
        for (int d = 0; d < 3; ++d) {
            fill3();
            US ul = { (USHORT)srclen, (USHORT)(srclen + d * 2), (wchar_t*)(A_live + 2048) };
            US uu = { (USHORT)srclen, (USHORT)(srclen + d * 2), (wchar_t*)(A_ours + 2048) };
            REF_US ur = { (USHORT)srclen, (USHORT)(srclen + d * 2), (unsigned short*)(A_ref + 2048) };
            sys(&ul, &ul); wia_copyus(&uu, &uu); ref_copyus(&ur, &ur);
            ++checks;
            if (ul.Length != uu.Length || ul.Length != ur.Length ||
                memcmp(A_live, A_ours, ARENA) || memcmp(A_live, A_ref, ARENA)) {
                printf("FAIL [self] srclen=%u d=%d: live=%u ours=%u ref=%u\n",
                       srclen, d, ul.Length, uu.Length, ur.Length);
                ++failures;
            }
        }
    }

    /* ---- 6. page guard, both sides ------------------------------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        g_pagesize = si.dwPageSize;
        g_base = (unsigned char*)VirtualAlloc(NULL, g_pagesize * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!g_base || !VirtualAlloc(g_base, g_pagesize, MEM_COMMIT, PAGE_READWRITE)) {
            printf("FAIL: could not build the guard page\n"); return 2;
        }
        for (unsigned n = 0; n <= 200; ++n) {
            guard_dst(n, n);            /* exact fit: fills the buffer to the last byte */
            guard_dst(n, n + 1);        /* truncation lands exactly on the boundary */
            guard_dst(n, n + 64);       /* heavy truncation */
            if (n >= 2) guard_dst(n, n - 2);
            guard_src(n, 400);          /* source ends at the guard, destination roomy */
            guard_src(n, n);            /* and with the destination exactly the same size */
        }
    }

    /* ---- 7. large randomized fuzz, FIXED seed -------------------------------------------- */
    rs = 0x296C0DE296C0DEULL;
    for (int i = 0; i < 120000; ++i) {
        unsigned srclen, maxlen;
        unsigned r = rng();
        if (r & 1) srclen = rng() % 64;
        else if (r & 2) srclen = rng() % 600;
        else srclen = rng() % 4001;
        unsigned mode = rng() % 5;
        if (mode == 0) maxlen = srclen;
        else if (mode == 1) maxlen = srclen ? srclen - (rng() % srclen) - 0 : 0;
        else if (mode == 2) maxlen = srclen + (rng() % 8);
        else if (mode == 3) maxlen = rng() % 4200;
        else maxlen = srclen + 2;
        if (maxlen > 4200) maxlen = 4200;
        int dstoff = 1024 + (int)(rng() % 96);
        int srcoff = 8192 + (int)(rng() % 96);
        if ((rng() & 7) == 0) { srcoff = dstoff + (int)(rng() % 161) - 80; if (srcoff < 0) srcoff = 0; }
        run("fuzz", dstoff, maxlen, srclen, srcoff, (USHORT)rng());
    }

    if (!failures)
        printf("CORRECTNESS: PASS (%lld checks vs live ntdll!RtlCopyUnicodeString and reference.c: "
               "src=NULL, NULL buffer, len 0..200 x 10 MaximumLength shapes x 16 alignments, "
               "overlap -80..+80 at 28 lengths, self-copy, page guard on both sides 0..200, "
               "120k fuzz seed 0x296C0DE296C0DE)\n", checks);
    else
        printf("CORRECTNESS: FAIL (%lld failures out of %lld checks)\n", failures, checks);
    return failures ? 1 : 0;
}
