/* discovery/sid_primitives.c
   The ntdll SID primitives, timed before any of them is called a target.

   WHY. The SID *formatters* are all converted already (067 RtlConvertSidToUnicodeString, 269-272
   the ConvertStringSidToSid family), but the primitives underneath them are untouched, and
   tools/uncovered-exports.py still lists every one of them: RtlEqualSid, RtlEqualPrefixSid,
   RtlCopySid, RtlLengthSid, RtlValidSid, RtlLengthRequiredSid. They are attractive on paper for
   exactly the reasons the NLS functions are not -- a SID is a fixed binary layout with no locale,
   no code page, no collation table and no heap:

       UCHAR Revision; UCHAR SubAuthorityCount; UCHAR IdentifierAuthority[6]; ULONG SubAuthority[n];

   so the whole object is 8 + 4n bytes, n <= 15, and everything these functions do is a length
   computation or a memcmp of at most 68 bytes. That is a shape this repository normally wins.

   WHAT THIS MEASURES, and why each column is here:

     * ns per call at the four SIDs that actually occur. S-1-5-18 (LocalSystem, n=1, 12 bytes),
       S-1-5-32-544 (Administrators, n=2, 16 bytes), a real user account (n=5, 28 bytes) and the
       maximum legal SID (n=15, 68 bytes). Access checks are dominated by the n=5 case.
     * bytes per nanosecond, the same diagnostic that found change 225: ~4-5 is a byte loop, ~22 is
       16-byte SSE2, ~85 is 32-byte AVX2. At 12-68 bytes the number also says something blunter --
       whether ANY loop is visible above the call overhead.
     * the same function on the smallest and the largest SID. If the two are within noise then the
       cost is the call and not the work, and no assembly can fix a call. That is the single most
       likely outcome here and the reason this probe exists rather than a change directory.
     * equal vs differing-in-the-last-subauthority for RtlEqualSid, because a comparison that exits
       early on the first mismatch is a different function from one that always reads the whole SID,
       and the difference decides whether a 32-byte compare is even applicable.

   NOTHING HERE IS A CONTRACT. It is a shortlist. A candidate becomes a target only if the measured
   cost is in the work rather than in the call, and discovery/README.md is largely a record of
   functions that looked slow and turned out not to be.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef ULONG    (NTAPI *PFN_LEN)      (PSID);
typedef BOOLEAN  (NTAPI *PFN_VALID)    (PSID);
typedef BOOLEAN  (NTAPI *PFN_EQ)       (PSID, PSID);
typedef LONG     (NTAPI *PFN_COPY)     (ULONG, PSID, PSID);
typedef ULONG    (NTAPI *PFN_REQ)      (ULONG);
typedef PULONG   (NTAPI *PFN_SUBAUTH)  (PSID, ULONG);

static PFN_LEN     pLengthSid;
static PFN_VALID   pValidSid;
static PFN_EQ      pEqualSid;
static PFN_EQ      pEqualPrefixSid;
static PFN_COPY    pCopySid;
static PFN_REQ     pLengthRequiredSid;
static PFN_SUBAUTH pSubAuthoritySid;

/* ---- a SID built by hand, so the layout is explicit and no allocator is involved ---- */
typedef struct { UCHAR Rev; UCHAR Count; UCHAR Auth[6]; ULONG Sub[15]; } RAWSID;

static void mk(RAWSID* s, int n, ULONG seed) {
    memset(s, 0, sizeof *s);
    s->Rev = 1; s->Count = (UCHAR)n;
    s->Auth[5] = 5;                                  /* NT Authority */
    for (int i = 0; i < n; ++i) s->Sub[i] = seed + (ULONG)i * 7919u;
}
static int sidbytes(int n) { return 8 + 4 * n; }

/* ---- min-of-N timing, same shape as harness/bench.h so the numbers are comparable ---- */
static double freq_(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }
static volatile uint64_t sink;

#define TIME_BLOCK(NAME, BYTES, BODY)                                                    \
    do {                                                                                 \
        double fq = freq_(); int inner = 256; double best = 1e300;                        \
        for (int w = 0; w < 64; ++w) { BODY; }                                            \
        for (;;) {                                                                        \
            LARGE_INTEGER a,b; QueryPerformanceCounter(&a);                               \
            for (int i = 0; i < inner; ++i) { BODY; }                                     \
            QueryPerformanceCounter(&b);                                                  \
            double ns = (double)(b.QuadPart-a.QuadPart)*1e9/fq;                           \
            if (ns >= 300000.0 || inner >= (1<<24)) break;                                \
            inner *= 4;                                                                   \
        }                                                                                 \
        for (int t = 0; t < 200; ++t) {                                                   \
            LARGE_INTEGER a,b; QueryPerformanceCounter(&a);                               \
            for (int i = 0; i < inner; ++i) { BODY; }                                     \
            QueryPerformanceCounter(&b);                                                  \
            double ns = (double)(b.QuadPart-a.QuadPart)*1e9/fq/(double)inner;              \
            if (ns < best) best = ns;                                                      \
        }                                                                                  \
        printf("  %-24s %6d B  %8.2f ns  %8.2f B/ns\n", NAME, (int)(BYTES), best,          \
               (BYTES) > 0 ? (double)(BYTES)/best : 0.0);                                  \
    } while (0)

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    pLengthSid         = (PFN_LEN)    GetProcAddress(nt, "RtlLengthSid");
    pValidSid          = (PFN_VALID)  GetProcAddress(nt, "RtlValidSid");
    pEqualSid          = (PFN_EQ)     GetProcAddress(nt, "RtlEqualSid");
    pEqualPrefixSid    = (PFN_EQ)     GetProcAddress(nt, "RtlEqualPrefixSid");
    pCopySid           = (PFN_COPY)   GetProcAddress(nt, "RtlCopySid");
    pLengthRequiredSid = (PFN_REQ)    GetProcAddress(nt, "RtlLengthRequiredSid");
    pSubAuthoritySid   = (PFN_SUBAUTH)GetProcAddress(nt, "RtlSubAuthoritySid");
    if (!pLengthSid || !pEqualSid || !pCopySid) { printf("missing export\n"); return 2; }

    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    static const int NS[] = { 1, 2, 5, 15 };
    static const char* WHAT[] = { "S-1-5-18", "S-1-5-32-544", "user account", "max legal SID" };

    for (int k = 0; k < 4; ++k) {
        int n = NS[k], nb = sidbytes(n);
        RAWSID a, b, c, dst;
        mk(&a, n, 0x10000); mk(&b, n, 0x10000);          /* equal */
        mk(&c, n, 0x10000); c.Sub[n-1] ^= 1u;            /* differ in the LAST subauthority */

        printf("\n== %s  (n=%d, %d bytes) ==\n", WHAT[k], n, nb);
        TIME_BLOCK("RtlLengthSid",        nb, sink ^= pLengthSid(&a));
        TIME_BLOCK("RtlValidSid",         nb, sink ^= pValidSid(&a));
        TIME_BLOCK("RtlEqualSid equal",   nb, sink ^= pEqualSid(&a, &b));
        TIME_BLOCK("RtlEqualSid last-dif",nb, sink ^= pEqualSid(&a, &c));
        TIME_BLOCK("RtlEqualPrefixSid",   nb, sink ^= pEqualPrefixSid(&a, &b));
        TIME_BLOCK("RtlCopySid",          nb, sink ^= (uint64_t)pCopySid(sizeof dst, &dst, &a));
        TIME_BLOCK("RtlSubAuthoritySid",   0, sink ^= (uint64_t)(uintptr_t)pSubAuthoritySid(&a, 0));
    }
    printf("\n== scalar-arg control (no SID is read at all) ==\n");
    TIME_BLOCK("RtlLengthRequiredSid", 0, sink ^= pLengthRequiredSid(5));

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
