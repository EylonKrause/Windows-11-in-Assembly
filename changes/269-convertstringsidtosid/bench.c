/* changes/269-convertstringsidtosid/bench.c
 *
 * Gate 2: time wia_str2sid against the live advapi32!ConvertStringSidToSidW.
 *
 * The rows are the shapes the implementation distinguishes, not a size sweep, because this function
 * has no size in the usual sense; it has a COUNT, and a handful of paths that a count does not
 * reach:
 *
 *   1..8 sub-authorities   the cost per number, which is the whole point: discovery measured the
 *                          shipped export at ~45 ns per decimal number
 *   the alias             two characters and a table lookup
 *   the hexadecimal carry  a `0x` revision, which changes how every later field is read
 *   Unicode digits         the lenient parser's other digit blocks
 *   a refusal              the failing path, which callers hit as often as the succeeding one and
 *                          which no "how fast does it parse" row would measure
 *
 * Every row allocates and frees, on both sides, because the contract returns a LocalAlloc block and
 * about 40 ns of any answer is that allocation. A row that leaked would measure the allocator
 * warming up; a row that skipped the free would measure a different allocator entirely.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

BOOL wia_str2sid(const wchar_t*, PSID*);
int  wia_sid_alias_init(void);
int  wia_sid_classify_init(void);

typedef BOOL (WINAPI *FN)(LPCWSTR, PSID*);
static FN sys;

typedef struct { const wchar_t* s; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) {
        PSID p = 0;
        if (wia_str2sid(m->s, &p) && p) { acc += *(unsigned char*)p; LocalFree(p); }
        else acc += 1;
    }
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) {
        PSID p = 0;
        if (sys(m->s, &p) && p) { acc += *(unsigned char*)p; LocalFree(p); }
        else acc += 1;
    }
    return acc;
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"advapi32.dll");
    static const wchar_t* S[] = {
        L"S-1-5-1",
        L"S-1-5-1-2",
        L"S-1-5-21-1",
        L"S-1-5-21-305419896-2596069104",
        L"S-1-5-21-305419896-2596069104-287454020-1001",
        L"S-1-5-21-1-2-3-4-5-6",
        L"S-1-5-21-1-2-3-4-5-6-7-8",
        L"BA",
        L"LA",
        L"S-0x1-5-21-1a2b-3c4d-5e6f-1001",
        /* The row must reach the path it is named for. The first version of this row was
           `S-1-<0661><0662>-<0967><0968>-<0E51>` -- Arabic-Indic revision, Devanagari authority,
           THAI sub-authority -- and the Thai digits are in the LENIENT table but not the STRICT
           one, so it was a REFUSAL that never allocated, timed at 8.5 ns per call and labelled
           "Unicode digits". It measured the refusal path twice and the digit tables not at all.
           The succeeding form puts the non-ASCII digits where each parser accepts them:
           Arabic-Indic revision, Devanagari authority, FULLWIDTH sub-authorities. It resolves to
           S-1-12-2019-556. The refusing form is kept as its own row, one line down, because the
           asymmetry between the two digit sets is a real path and now it is named for what it is. */
        L"S-\x0661-\x0967\x0968-\xFF12\xFF10\xFF11\xFF19-\xFF15\xFF15\xFF16",
        L"S-1-\x0661\x0662-\x0967\x0968-\x0E51",
        L"S-1-5-21-305419896-2596069104-287454020-100x",
        L"not-a-sid-at-all"
    };
    static const char* N[] = {
        "1 sub-authority", "2 sub-authorities", "2, realistic", "4 sub-authorities",
        "5, a real account SID", "8 sub-authorities", "10 sub-authorities",
        "the alias BA", "the alias LA (machine)", "hex carry, 6 fields",
        "Unicode digits, accepted", "Unicode digits, strict refusal",
        "a refusal, late", "a refusal, immediate"
    };
    enum { K = 14 };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i;

    sys = (FN)GetProcAddress(h, "ConvertStringSidToSidW");
    if (!sys) { printf("resolve failed\n"); return 1; }
    if (wia_sid_classify_init()) { printf("the character classes failed to build\n"); return 1; }
    if (wia_sid_alias_init())    { printf("the alias table failed to build\n"); return 1; }

    /* Pre-flight: Say what each row actually does before timing it. a row whose name promises the
       alias table, the hexadecimal carry or the Unicode digits and which in fact refuses on its
       first field is timing the refusal path under another row's name -- change 210 shipped a
       "table path" row that compared identical strings and short-circuited in tier one, and the
       first draft of this bench shipped a "Unicode digits" row that was a refusal. This loop makes
       that visible, and the three rows whose names say "refusal" are the only ones allowed to be
       one. */
    {
        /* stated per row rather than derived from the label, so that renaming a row cannot
           silently change what it is allowed to do */
        static const int refuse_row[K] = { 0,0,0,0,0,0,0,0,0,0, 0, 1, 1, 1 };
        int bad = 0;
        printf("  pre-flight (what each row reaches):\n");
        for (i = 0; i < K; ++i) {
            PSID p = 0;
            BOOL r = sys(S[i], &p);
            printf("    %-32s %s", N[i], r ? "ACCEPTED" : "refused ");
            if (r && p) { printf("  %lu bytes", (unsigned long)GetLengthSid(p)); LocalFree(p); }
            else        { printf("  err=%lu", (unsigned long)GetLastError()); }
            printf("\n");
            if (r == !refuse_row[i]) continue;
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            ++bad;
        }
        if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }
    }

    for (i = 0; i < K; ++i) {
        cx[i].s = S[i];
        cx[i].reps = 8;                      /* every answer is tens of nanoseconds; x8 keeps the
                                                row above the harness floor (change 261) */
        cs[i].label = N[i];
        cs[i].bytes = (size_t)lstrlenW(S[i]) * 2 * (size_t)cx[i].reps;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    return wia_bench_compare(
        "advapi32 ConvertStringSidToSidW  (wia vs the shipped parser, x8 calls per row)",
        cs, K, 200);
}
