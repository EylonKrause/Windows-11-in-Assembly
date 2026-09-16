/* changes/244-hashdata/probes/overlap.c
 *
 * THE ONE CASE THAT FIXES THE LOOP ORDER, and the reason a fast implementation needs a fallback.
 *
 * With disjoint buffers the digest bytes are independent chains, so an implementation may compute
 * them in ANY order and in any grouping -- which is exactly what makes this function worth
 * rewriting: twelve chains can be held in registers and advanced together, while the shipped code
 * walks one lane at a time through memory.
 *
 * When the digest OVERLAPS THE SOURCE the independence disappears. The shipped inner loop re-reads
 * pbData[i] on every lane iteration (`movzx edx, byte ptr [r10 + rdi]` sits INSIDE the lane loop),
 * so a lane write that lands on pbData[i] changes what the remaining lanes of that same source byte
 * consume. The answer then depends on:
 *
 *     * the LANE ORDER    -- ascending or descending j,
 *     * the GROUPING      -- whether lanes are advanced one source byte at a time (shipped) or
 *                            twelve lanes at a time across the whole source (fast),
 *     * whether the seed is written to the caller's buffer before the source is read.
 *
 * So this probe measures which of those the export actually does, over deliberately overlapping
 * buffers at every relative offset. Whatever it says, the implementation must either reproduce it
 * or refuse to take its fast path on overlap -- and the only way to know which is required is to
 * measure it rather than assume the case cannot arise.
 *
 * Three models are compared:
 *     DESC  : seed into the caller's buffer, then for i descending, for j DESCENDING, re-reading src
 *     ASC   : the same but for j ASCENDING
 *     GROUP : the fast shape -- lanes held privately in groups of twelve, each group advanced over
 *             the whole source, the digest written back at the end
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef HRESULT (WINAPI *FN_HASH)(const BYTE*, DWORD, BYTE*, DWORD);
static FN_HASH sys;
static unsigned char T[256];

static void model_desc(unsigned char* buf, DWORD dataoff, DWORD n, DWORD hashoff, DWORD m)
{
    unsigned char* src = buf + dataoff;
    unsigned char* h   = buf + hashoff;
    for (DWORD j = 0; j < m; ++j) h[j] = (unsigned char)j;
    for (DWORD i = n; i-- > 0; )
        for (DWORD j = m; j-- > 0; )
            h[j] = T[h[j] ^ src[i]];
}
static void model_asc(unsigned char* buf, DWORD dataoff, DWORD n, DWORD hashoff, DWORD m)
{
    unsigned char* src = buf + dataoff;
    unsigned char* h   = buf + hashoff;
    for (DWORD j = 0; j < m; ++j) h[j] = (unsigned char)j;
    for (DWORD i = n; i-- > 0; )
        for (DWORD j = 0; j < m; ++j)
            h[j] = T[h[j] ^ src[i]];
}
static void model_group(unsigned char* buf, DWORD dataoff, DWORD n, DWORD hashoff, DWORD m)
{
    unsigned char* src = buf + dataoff;
    unsigned char* h   = buf + hashoff;
    unsigned char lane[12];
    for (DWORD j = 0; j < m; ++j) h[j] = (unsigned char)j;
    for (DWORD start = 0; start < m; start += 12) {
        DWORD L = (m - start < 12) ? m - start : 12;
        for (DWORD k = 0; k < L; ++k) lane[k] = (unsigned char)(start + k);
        for (DWORD i = n; i-- > 0; ) {
            unsigned char c = src[i];
            for (DWORD k = 0; k < L; ++k) lane[k] = T[lane[k] ^ c];
        }
        for (DWORD k = 0; k < L; ++k) h[start + k] = lane[k];
    }
}

int main(void)
{
    HMODULE hm = LoadLibraryW(L"shlwapi.dll");
    sys = (FN_HASH)GetProcAddress(hm, "HashData");
    if (!sys) { printf("cannot resolve HashData\n"); return 1; }
    for (int v = 0; v < 256; ++v) {
        unsigned char s = (unsigned char)v, d = 0;
        sys(&s, 1, &d, 1);
        T[v] = d;
    }

    printf("HashData: overlapping source and digest\n\n");

    enum { BUF = 256 };
    static unsigned char live[BUF], a[BUF], b[BUF], c[BUF], seedbuf[BUF];
    for (int i = 0; i < BUF; ++i) seedbuf[i] = (unsigned char)(i * 37 + 11);

    long cases = 0, dmatch = 0, amatch = 0, gmatch = 0, none = 0;
    long dis_cases = 0, dis_gmatch = 0;

    /* every relative placement of a 20-byte digest against a 24-byte source inside one buffer */
    for (int hoff = 0; hoff <= 48; ++hoff) {
        for (int doff = 0; doff <= 48; ++doff) {
            DWORD n = 24, m = 20;
            int overlap = (doff < hoff + (int)m) && (hoff < doff + (int)n);
            memcpy(live, seedbuf, BUF); memcpy(a, seedbuf, BUF);
            memcpy(b, seedbuf, BUF);    memcpy(c, seedbuf, BUF);
            sys(live + doff, n, live + hoff, m);
            model_desc (a, doff, n, hoff, m);
            model_asc  (b, doff, n, hoff, m);
            model_group(c, doff, n, hoff, m);
            int dm = memcmp(live, a, BUF) == 0;
            int am = memcmp(live, b, BUF) == 0;
            int gm = memcmp(live, c, BUF) == 0;
            ++cases;
            if (dm) ++dmatch;
            if (am) ++amatch;
            if (gm) ++gmatch;
            if (!dm && !am && !gm) ++none;
            if (!overlap) { ++dis_cases; if (gm) ++dis_gmatch; }
        }
    }
    printf("placements tried: %ld  (a 24-byte source and a 20-byte digest at every offset pair)\n",
           cases);
    printf("  matches DESCENDING-lane, re-reading the source : %ld\n", dmatch);
    printf("  matches ASCENDING-lane                         : %ld\n", amatch);
    printf("  matches the GROUPED (twelve lanes in registers) shape: %ld\n", gmatch);
    printf("  matches none of the three                      : %ld\n", none);
    printf("  of the %ld DISJOINT placements, the grouped shape matched %ld\n\n",
           dis_cases, dis_gmatch);

    if (dmatch == cases)
        printf("VERDICT: the export is DESCENDING-lane with the source re-read per lane, on every\n"
               "placement including the overlapping ones.\n");
    else if (dmatch > gmatch)
        printf("VERDICT: descending-lane explains more placements than the grouped shape, so a\n"
               "grouped implementation MUST NOT take its fast path when the buffers overlap.\n");
    if (gmatch < cases)
        printf("         The grouped shape disagrees on %ld placements -- all of them overlapping,\n"
               "         which is what the fallback exists for.\n", cases - gmatch);
    if (dis_gmatch == dis_cases)
        printf("         On every DISJOINT placement all three models agree, which is the licence\n"
               "         to reorder and group the lanes at all.\n");
    return 0;
}
