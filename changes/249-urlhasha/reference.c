/* changes/249-urlhasha/reference.c
 *
 * An INDEPENDENT oracle for UrlHashA.
 *
 * It recovers the permutation table at runtime rather than carrying a copy of it. 256 calls to the
 * live HashData export with a one-byte source and a one-byte digest give T directly, because
 *
 *     h[0] = T[ seed[0] ^ src[0] ] = T[ 0 ^ b ] = T[b]
 *
 * That is deliberate on two counts. It means this file shares NO constant with impl.asm's chain --
 * not change 244's c_tab, not change 244's own hard-coded REF_T, so a wrong table cannot agree
 * with itself across the comparison. And it recovers the table from a DIFFERENT EXPORT than the one
 * under test: UrlHashA reaches the worker at RVA 0xC0A10, HashData is the body at 0xBB750, and the
 * disassembly says both `lea` their table from RVA 0x2A6010. Recovering through one and testing
 * through the other is a check on that claim rather than a restatement of it.
 *
 * The rule itself, as change 244 measured it against the live export:
 *
 *     h[j] = (BYTE)j                                 for j = 0 .. cbHash-1   (the seed; it WRAPS)
 *     for i = n-1 down to 0:                                                 (Last byte first)
 *         for j = cbHash-1 down to 0:
 *             h[j] = T[ h[j] ^ url[i] ]
 *
 * and UrlHashA's own envelope around it, measured by probes/urlhash.c:
 *
 *     pszUrl NULL or pbHash NULL -> 0x80070057, digest untouched;
 *     cbHash is NOT VALIDATED; every value from 0 to 256 returns S_OK, 0 writes nothing, and
 *     nothing past cbHash is ever written;
 *     n comes from lstrlenA, and the return is S_OK UNCONDITIONALLY.
 *
 * It writes through the caller's buffer on purpose, seed included. Accumulating in a local and
 * copying out at the end is equivalent for every sane call and NOT equivalent when the digest
 * overlaps the URL, because then the source bytes still to be read have already been rewritten by
 * earlier digest writes. The shipped loop updates in place, so this must, or it cannot judge the
 * overlapping placements at all, which are precisely the ones change 244's grouped kernel has to
 * hand to its byte-for-byte fallback.
 *
 * What it is not asked: a url that faults. The oracle would have to fault to discover the length,
 * and the shipped function swallows that fault via lstrlenA's SEH handler. correctness.c tests that
 * case against the live export alone.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#define REF_INVALID ((HRESULT)0x80070057L)

static unsigned char T[256];
static int T_ready = 0;

typedef HRESULT (WINAPI *FH)(const BYTE*, DWORD, BYTE*, DWORD);

/* Returns 0 if the table could not be recovered, and the count of distinct entries otherwise --
   256 for a permutation, less for anything else, which the caller reports rather than ignores. */
int ref_init(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    FH hd = (FH)GetProcAddress(h, "HashData");
    int b, seen[256], distinct = 0;

    if (!hd) return 0;
    memset(seen, 0, sizeof seen);
    for (b = 0; b < 256; ++b) {
        BYTE src = (BYTE)b, out = 0;
        if (hd(&src, 1, &out, 1) != S_OK) return 0;
        T[b] = out;
        if (!seen[out]) { seen[out] = 1; ++distinct; }
    }
    T_ready = 1;
    return distinct;
}

const unsigned char* ref_table(void) { return T; }

HRESULT ref_urlhasha(const char* pszUrl, BYTE* pbHash, DWORD cbHash)
{
    size_t n, i;
    DWORD j;

    if (!pszUrl || !pbHash) return REF_INVALID;   /* and the digest stays untouched */
    if (!T_ready) return (HRESULT)0xDEAD0001L;    /* ref_init was not called: fail loudly */

    /* The length first, and the order matters when they overlap. The shipped envelope calls
       lstrlenA at 0x12F772 and only then the worker at 0x12F782, so the length is taken from the
       URL as the caller passed it. Seeding first would be equivalent for every disjoint call and
       wrong whenever the digest lands on the URL -- the seed would move or erase its terminator and
       the hash would run over a different number of bytes. */
    n = strlen(pszUrl);

    for (j = 0; j < cbHash; ++j) pbHash[j] = (BYTE)j;   /* the seed WRAPS at 256 */

    for (i = n; i-- > 0; ) {                            /* LAST BYTE FIRST */
        for (j = cbHash; j-- > 0; )
            pbHash[j] = T[pbHash[j] ^ (unsigned char)pszUrl[i]];
    }
    return S_OK;                                        /* unconditionally, as the shipped one does */
}
