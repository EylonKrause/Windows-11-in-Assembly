/* changes/270-convertsidtostringsid/bench.c
 *
 * Gate 2: time wia_sid2str against the live advapi32!ConvertSidToStringSidW.
 *
 * EVERY ROW ALLOCATES AND FREES, on both sides, because the contract returns a LocalAlloc block the
 * caller frees and discovery/sid_inet_bstr.c measured that pair at 40.41 ns -- about a quarter of
 * the shipped export's 181 ns for a five-sub-authority SID. A row that leaked would measure the
 * allocator warming up; a row that skipped the free would measure a different allocator entirely.
 *
 * THE ROWS ARE THE SHAPES THE IMPLEMENTATION DISTINGUISHES, because this function has no size in
 * the usual sense -- it has a COUNT, and a handful of paths a count does not reach:
 *
 *   0, 1, 2, 5, 8, 15    the per-number cost, which is what change 067's rewrite changed
 *   short sub-authorities  the same fifteen numbers with a third of the digits
 *   the hex authority    a different converter, reached only above 2^32
 *   two refusals         a bad revision and a count above 15, which a caller hits as often as the
 *                        succeeding case and which no "how fast does it format" row measures
 *
 * EVERY ROW IS PRE-FLIGHTED against a per-row table of what the live export must return. A row
 * named for a path it does not reach is timing something else under that name -- change 210 shipped
 * one, and so did change 269's first bench.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

extern BOOL wia_sid2str(const void*, wchar_t**);
typedef BOOL (WINAPI *FN)(PSID, LPWSTR*);
static FN sys;

typedef struct { unsigned char sid[8 + 4 * 16]; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    wchar_t* p = 0;
    if (wia_sid2str(m->sid, &p) && p) { uint64_t v = (uint64_t)p[0]; LocalFree(p); return v; }
    return 1;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    LPWSTR p = 0;
    if (sys((PSID)m->sid, &p) && p) { uint64_t v = (uint64_t)p[0]; LocalFree(p); return v; }
    return 1;
}

static void mk(unsigned char* sid, unsigned rev, unsigned long long auth,
               unsigned cnt, const unsigned* sub)
{
    unsigned i;
    sid[0] = (unsigned char)rev;
    sid[1] = (unsigned char)cnt;
    for (i = 0; i < 6; ++i) sid[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < cnt && i < 16; ++i) {
        sid[8 + 4 * i + 0] = (unsigned char)(sub[i]);
        sid[8 + 4 * i + 1] = (unsigned char)(sub[i] >> 8);
        sid[8 + 4 * i + 2] = (unsigned char)(sub[i] >> 16);
        sid[8 + 4 * i + 3] = (unsigned char)(sub[i] >> 24);
    }
}

enum { K = 10 };

int main(void)
{
    static const char* N[K] = {
        "0 sub-authorities", "1 sub-authority", "2 sub-authorities",
        "5, a real account SID", "8 sub-authorities", "15, the maximum",
        "15, short (1-3 digits)", "hex authority, 5 subs",
        "a refusal: revision 2", "a refusal: count 16"
    };
    static const int WANT_OK[K] = { 1,1,1,1,1,1,1,1, 0,0 };
    static ctx_t cx[K];
    static wia_case cs[K];
    static unsigned big[16], small[16];
    int i, bad = 0;

    sys = (FN)GetProcAddress(LoadLibraryW(L"advapi32.dll"), "ConvertSidToStringSidW");
    if (!sys) { printf("no ConvertSidToStringSidW\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 16; ++i) { big[i] = 1000000000u + (unsigned)i * 271828183u; small[i] = 1 + i * 37; }
    big[0] = 21; big[1] = 305419896u; big[2] = 2596069104u; big[3] = 287454020u; big[4] = 1001;

    mk(cx[0].sid, 1, 5, 0,  big);
    mk(cx[1].sid, 1, 5, 1,  big);
    mk(cx[2].sid, 1, 5, 2,  big);
    mk(cx[3].sid, 1, 5, 5,  big);
    mk(cx[4].sid, 1, 5, 8,  big);
    mk(cx[5].sid, 1, 5, 15, big);
    mk(cx[6].sid, 1, 5, 15, small);
    mk(cx[7].sid, 1, 0x123456789ABCull, 5, big);
    mk(cx[8].sid, 2, 5, 5,  big);
    mk(cx[9].sid, 1, 5, 5,  big); cx[9].sid[1] = 16;

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < K; ++i) {
        LPWSTR p = 0;
        BOOL r;
        SetLastError(0);
        r = sys((PSID)cx[i].sid, &p);
        printf("    %-24s %s", N[i], r ? "ACCEPTED" : "refused ");
        if (r && p) { printf("  %d chars", lstrlenW(p)); }
        else        { printf("  err=%lu", (unsigned long)GetLastError()); }
        printf("\n");
        if ((r ? 1 : 0) != WANT_OK[i]) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            ++bad;
        }
        cs[i].label = N[i];
        cs[i].bytes = (r && p) ? (size_t)(lstrlenW(p) * 2) : 8;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
        if (p) LocalFree(p);
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("advapi32 ConvertSidToStringSidW  (wia envelope over change 067 vs the "
                             "shipped wrapper)", cs, K, 300);
}
