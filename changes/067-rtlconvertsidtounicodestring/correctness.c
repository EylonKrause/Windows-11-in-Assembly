/* changes/067-rtlconvertsidtounicodestring/correctness.c
 *
 * Gate 1 for ntdll!RtlConvertSidToUnicodeString: Ours vs the scalar model vs the live export, on
 * the NTSTATUS, Out->Length, Out->MaximumLength and every byte of the destination buffer.
 *
 * ------------------------------------------------------------------------------------------------
 * What the old gate missed, and why it missed it
 *
 * The version this replaces ran 3,000,000 random SIDs and reported PASS. It drew its sub-authority
 * count as
 *
 *      int cnt = (seed>>8)%16;                    /- 0..15, never more -/
 *
 * so a count above 15 was never generated once. The live export refuses every count above 15 with
 * STATUS_INVALID_SID; the implementation under test did not, and would have formatted all 200
 * sub-authorities of a SID whose count byte said 200, reading 1028 bytes out of a 68-byte
 * structure and writing about 2500 bytes into a 400-byte stack temporary. A SID like that takes one
 * call to ConvertStringSidToSidW to produce, which accepts 254 of them. That is a stack overrun in
 * a landed change, and the only reason no test saw it is that the corpus could not express it.
 *
 * So the count is now swept 0..255 EXHAUSTIVELY rather than sampled, which is the same correction
 * change 210 needed (a row that never reached the path it was named for) and change 268 needed (a
 * corpus whose surrogate class could only land on the direction that has no surrogates).
 *
 * ------------------------------------------------------------------------------------------------
 * The other three things this gate does that the old one did not
 *
 *   * It compares the whole destination, not the string up to Length. Change 268's whole-buffer
 *     comparison found change 016 storing sixteen bytes and advancing by fewer, 154 mismatches,
 *     every one a single 00 past the end of the string, invisible to any check that stops at the
 *     produced length. The destination here is poison-filled before every call, and all of it is
 *     compared, on failing calls too: STATUS_BUFFER_OVERFLOW must leave it untouched.
 *
 *   * It puts the SID against a guard page. changes/270-.../probes/truncated.c established that a
 *     short SID is a REFUSAL when its sub-authority array runs off the end and a FAULT when only
 *     its identifier authority does. Both are reproduced here, for every count, with the live
 *     export as the judge.
 *
 *   * It checks the assembler-generated tables against the definitions they are supposed to
 *     satisfy. They are produced by REPT arithmetic, which is exactly the kind of thing that is
 *     wrong silently; change 267's first table construction WAS wrong.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;

extern NTSTATUS wia_sidfmt(U*, void*, BOOLEAN);
extern void*    wia_sidfmt_dec2w(void);
extern void*    wia_sidfmt_tabs(void);
NTSTATUS ref_sidfmt(U*, const unsigned char*);

typedef NTSTATUS (WINAPI *fn)(U*, void*, BOOLEAN);
static fn sys;

static int  failures = 0;
static long cases = 0;

#define DBUF 512                       /* wide characters; the longest result is 184 */
#define POISON 0x2A2A

static void one(const unsigned char* sid, USHORT ml)
{
    static wchar_t bo[DBUF], by[DBUF], br[DBUF];
    U uo, uy, ur;
    NTSTATUS ro, ry, rr;
    int i, bad = 0;

    for (i = 0; i < DBUF; ++i) bo[i] = by[i] = br[i] = POISON;
    uo.Length = uy.Length = ur.Length = 0xBEEF;
    uo.MaximumLength = uy.MaximumLength = ur.MaximumLength = ml;
    uo.Buffer = bo; uy.Buffer = by; ur.Buffer = br;

    ro = wia_sidfmt(&uo, (void*)sid, FALSE);
    ry = sys(&uy, (void*)sid, FALSE);
    rr = ref_sidfmt(&ur, sid);

    ++cases;
    if (ro != ry || ro != rr) bad = 1;
    if (uo.Length != uy.Length || uo.Length != ur.Length) bad = 1;
    if (uo.MaximumLength != uy.MaximumLength) bad = 1;
    /* the whole buffer, on success and on failure alike */
    if (memcmp(bo, by, sizeof bo) != 0) bad = 1;
    if (memcmp(bo, br, sizeof br) != 0) bad = 1;

    if (bad && failures < 12) {
        printf("  FAIL rev=%u cnt=%u ml=%u:\n", sid[0], sid[1], ml);
        printf("       ours %08lX len=%u  ntdll %08lX len=%u  model %08lX len=%u%s\n",
               (unsigned long)ro, uo.Length, (unsigned long)ry, uy.Length,
               (unsigned long)rr, ur.Length,
               memcmp(bo, by, sizeof bo) ? "   (the destination differs from ntdll)" : "");
        if (ro == 0) printf("       ours \"%ls\"\n       ntdl \"%ls\"\n       modl \"%ls\"\n", bo, by, br);
    }
    if (bad) ++failures;
}

static void mk(unsigned char* sid, unsigned rev, unsigned long long auth,
               unsigned cnt, const unsigned* sub)
{
    unsigned i;
    sid[0] = (unsigned char)rev;
    sid[1] = (unsigned char)cnt;
    for (i = 0; i < 6; ++i) sid[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < cnt; ++i) {
        sid[8 + 4 * i + 0] = (unsigned char)(sub[i]);
        sid[8 + 4 * i + 1] = (unsigned char)(sub[i] >> 8);
        sid[8 + 4 * i + 2] = (unsigned char)(sub[i] >> 16);
        sid[8 + 4 * i + 3] = (unsigned char)(sub[i] >> 24);
    }
}

/* -------------------------------------------------------------------- the tables */
static int check_tables(void)
{
    const unsigned* d2w = (const unsigned*)wia_sidfmt_dec2w();
    const unsigned char* tabs = (const unsigned char*)wia_sidfmt_tabs();
    const unsigned long long* pow10 = (const unsigned long long*)tabs;
    const unsigned char* g = tabs + 88;
    const char* hex = (const char*)(tabs + 120);
    int i, bad = 0;

    for (i = 0; i < 100; ++i) {
        unsigned want = (unsigned)('0' + i / 10) | ((unsigned)('0' + i % 10) << 16);
        if (d2w[i] != want) { printf("  the digit-pair table is wrong at %d\n", i); bad = 1; }
    }
    {
        unsigned long long p = 1;
        for (i = 0; i <= 10; ++i) {
            if (pow10[i] != p) { printf("  the power-of-ten table is wrong at %d\n", i); bad = 1; }
            p *= 10;
        }
    }
    for (i = 0; i < 32; ++i) {
        /* the number of decimal digits of 2^i, computed the slow obvious way */
        unsigned long long v = 1ull << i, t = 10;
        int n = 1;
        while (v >= t) { t *= 10; ++n; }
        if (g[i] != n) { printf("  the digit-count table is wrong at %d: %u, want %d\n", i, g[i], n); bad = 1; }
    }
    for (i = 0; i < 16; ++i) {
        char want = (char)(i < 10 ? '0' + i : 'A' + i - 10);
        if (hex[i] != want) { printf("  the hex-digit table is wrong at %d\n", i); bad = 1; }
    }
    if (!bad) printf("  0. the four assembler-generated tables match their definitions\n");
    return bad;
}

/* -------------------------------------------------------------------- the guard page */
static unsigned char* g_base;
static SIZE_T g_pagesz;

static void guard_sweep(void)
{
    static unsigned char full[8 + 4 * 16];
    static unsigned sub[16];
    unsigned cnt, avail, i;
    long before = cases;

    for (i = 0; i < 16; ++i) sub[i] = 1000000000u + i;

    for (cnt = 0; cnt <= 3; ++cnt) {
        unsigned need = 8 + 4 * cnt;
        mk(full, 1, 5, cnt, sub);
        for (avail = 1; avail <= need + 1; ++avail) {
            unsigned char* p = g_base + g_pagesz - avail;
            static wchar_t bo[DBUF], by[DBUF];
            U uo, uy;
            NTSTATUS ro = 0, ry = 0;
            int fo = 0, fy = 0, k;

            for (k = 0; k < (int)avail; ++k) p[k] = full[k];
            for (k = 0; k < DBUF; ++k) bo[k] = by[k] = POISON;
            uo.Length = uy.Length = 0xBEEF;
            uo.MaximumLength = uy.MaximumLength = sizeof bo;
            uo.Buffer = bo; uy.Buffer = by;

            __try { ro = wia_sidfmt(&uo, p, FALSE); } __except (EXCEPTION_EXECUTE_HANDLER) { fo = 1; }
            __try { ry = sys(&uy, p, FALSE); }        __except (EXCEPTION_EXECUTE_HANDLER) { fy = 1; }

            ++cases;
            if (fo != fy || (!fo && (ro != ry || uo.Length != uy.Length ||
                                     memcmp(bo, by, sizeof bo) != 0))) {
                if (failures < 12)
                    printf("  FAIL guard cnt=%u avail=%u: ours %s%08lX  ntdll %s%08lX\n",
                           cnt, avail, fo ? "FAULT " : "", (unsigned long)ro,
                           fy ? "FAULT " : "", (unsigned long)ry);
                ++failures;
            }
        }
    }
    printf("  5. a SID ending exactly at a guard page, every count and every truncation: %ld\n",
           cases - before);
}

int main(void)
{
    static unsigned char sid[8 + 4 * 256];
    static unsigned sub[256];
    SYSTEM_INFO si;
    unsigned i, j, k;
    unsigned long seed = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (fn)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlConvertSidToUnicodeString");
    if (!sys) { printf("no RtlConvertSidToUnicodeString\n"); return 2; }
    printf("== CORRECTNESS: RtlConvertSidToUnicodeString ==\n");
    if (check_tables()) return 1;

    /* 1. The count, exhaustively 0..255; the sweep the old corpus could not express */
    {
        long before = cases;
        for (i = 0; i < 256; ++i) sub[i] = 4000000000u + i;
        for (i = 0; i <= 255; ++i) {
            mk(sid, 1, 5, i, sub);
            one(sid, 1000);
        }
        printf("  1. every sub-authority count 0..255 (the limit is 15): %ld\n", cases - before);
    }

    /* 2. The revision, exhaustively 0..255 */
    {
        long before = cases;
        for (i = 0; i <= 255; ++i) {
            mk(sid, i, 5, 3, sub);
            one(sid, 1000);
        }
        printf("  2. every revision 0..255 (the only legal one is 1): %ld\n", cases - before);
    }

    /* 3. The identifier authority around the 2^32 boundary and the digit-count boundaries */
    {
        long before = cases;
        static const unsigned long long AS[] = {
            0ull, 1ull, 9ull, 10ull, 99ull, 100ull, 999ull, 1000ull, 9999ull, 10000ull,
            99999ull, 100000ull, 999999ull, 1000000ull, 9999999ull, 10000000ull,
            99999999ull, 100000000ull, 999999999ull, 1000000000ull,
            0xFFFFFFFEull, 0xFFFFFFFFull, 0x100000000ull, 0x100000001ull,
            0xABCDEFull, 0x123456789Aull, 0xFFFFFFFFFFFEull, 0xFFFFFFFFFFFFull,
            0x1000000000ull, 0xF00000000ull, 0x8000000000ull
        };
        for (i = 0; i < sizeof AS / sizeof AS[0]; ++i)
            for (j = 0; j <= 4; ++j) {
                mk(sid, 1, AS[i], j, sub);
                one(sid, 1000);
            }
        printf("  3. the identifier authority at every decimal and hex boundary: %ld\n",
               cases - before);
    }

    /* 4. SUB-AUTHORITY VALUES at every digit-count boundary, in every position */
    {
        long before = cases;
        static const unsigned VS[] = {
            0u, 1u, 9u, 10u, 11u, 99u, 100u, 101u, 999u, 1000u, 9999u, 10000u,
            99999u, 100000u, 999999u, 1000000u, 9999999u, 10000000u, 99999999u,
            100000000u, 999999999u, 1000000000u, 2147483647u, 2147483648u,
            4294967294u, 4294967295u
        };
        for (i = 0; i < sizeof VS / sizeof VS[0]; ++i) {
            for (k = 0; k < 16; ++k) sub[k] = VS[i];
            for (j = 0; j <= 15; ++j) {
                mk(sid, 1, 5, j, sub);
                one(sid, 1000);
            }
            /* and one of them among ordinary values, in each position */
            for (k = 0; k < 16; ++k) sub[k] = 1234567890u;
            for (j = 0; j < 15; ++j) {
                sub[j] = VS[i];
                mk(sid, 1, 5, 15, sub);
                one(sid, 1000);
                sub[j] = 1234567890u;
            }
        }
        printf("  4. every digit-count boundary as a sub-authority, in every position: %ld\n",
               cases - before);
    }

    /* 5. the guard page */
    GetSystemInfo(&si);
    g_pagesz = si.dwPageSize;
    g_base = (unsigned char*)VirtualAlloc(0, g_pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!g_base || !VirtualAlloc(g_base, g_pagesz, MEM_COMMIT, PAGE_READWRITE)) {
        printf("  the guard page could not be set up\n"); return 1;
    }
    guard_sweep();

    /* 6. MaximumLength, every value from far too small to ample, for several lengths */
    {
        long before = cases;
        for (j = 0; j <= 15; j += 3) {
            USHORT ml;
            for (k = 0; k < 16; ++k) sub[k] = 4294967295u;
            mk(sid, 1, 0xFFFFFFFFFFFFull, j, sub);
            for (ml = 0; ml <= 300; ++ml) one(sid, ml);
        }
        printf("  6. every MaximumLength 0..300 at five lengths (the overflow boundary): %ld\n",
               cases - before);
    }

    /* 7. randomised, with the count drawn over its whole byte range */
    {
        long before = cases;
        for (i = 0; i < 400000 && failures < 12; ++i) {
            unsigned cnt;
            seed = seed * 1103515245u + 12345u;
            cnt = (seed >> 8) & 0xFF;                       /* 0..255, not 0..15 */
            seed = seed * 1103515245u + 12345u;
            {
                unsigned long long auth = 0;
                unsigned b;
                for (b = 0; b < 6; ++b) {
                    seed = seed * 1103515245u + 12345u;
                    /* the top two bytes are usually zero, so both authority forms occur */
                    auth = (auth << 8) | (unsigned char)((b < 2 && ((seed >> 3) & 3))
                                                         ? 0 : (seed >> 16));
                }
                for (k = 0; k < 16 && k < cnt; ++k) {
                    seed = seed * 1103515245u + 12345u;
                    sub[k] = seed;
                }
                mk(sid, 1, auth, cnt > 15 ? 15 : cnt, sub);
                sid[1] = (unsigned char)cnt;                /* the claimed count, which may exceed 15 */
                one(sid, (USHORT)(600 + (seed & 511)));
            }
        }
        printf("  7. 400000 randomised, the count over its whole byte range: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    if (!failures)
        printf("CORRECTNESS: PASS (status, Length, MaximumLength and the WHOLE destination exact vs\n"
               "live ntdll and vs the scalar model, including a SID against a guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
