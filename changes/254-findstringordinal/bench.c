/* changes/254-findstringordinal/bench.c
 *
 * OURS vs the LIVE kernelbase!FindStringOrdinal.
 *
 * The rows cover all four modes, because they have different costs and only two of them are
 * searches. FIND_STARTSWITH and FIND_ENDSWITH are a single comparison, so their row is dominated by
 * the ENVELOPE -- the validation, the last-error store, and the length resolution -- and is the row
 * where a vector implementation has the least to win and the most to lose. FIND_FROMEND is the half
 * with no precedent in change 252: a backward block walk, where finding the answer in the last block
 * should cost almost nothing and the shipped code still scans from the front.
 *
 * Both length forms are measured. a cch of -1 makes the function measure the string first, which is
 * change 001's wia_wcslen here and an unrolled scalar loop (RVA 0x0A1F4B) in the shipped code -- a
 * real difference that only shows up on that form.
 *
 * Every row states what it did before the table: the mode, the lengths, and the index ours and the
 * live export agreed on. A row labelled "miss" whose index is not -1 would be timing a short scan
 * and would still look entirely plausible; this project has twice measured a no-op and believed it.
 *
 * The adversarial row is kept from change 252 -- a needle whose first and last characters are both
 * the character the haystack is made of -- because that is the shape that made 252's gate report a
 * different verdict on the same code until the far anchor was CHOSEN rather than fixed at m-1.
 */
#include "bench.h"
#include <string.h>

#define F_STARTSWITH 0x00100000
#define F_ENDSWITH   0x00200000
#define F_FROMSTART  0x00400000
#define F_FROMEND    0x00800000

int wia_findstringordinal(DWORD, const wchar_t*, int, const wchar_t*, int, BOOL);
int wia_casemate_init(void);

typedef int (WINAPI *FFSO)(DWORD, LPCWSTR, int, LPCWSTR, int, BOOL);
static FFSO live;

typedef struct { DWORD fl; wchar_t* s; int cs; wchar_t* v; int cv; BOOL ic; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    return (uint64_t)(unsigned)wia_findstringordinal(c->fl, c->s, c->cs, c->v, c->cv, c->ic);
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    return (uint64_t)(unsigned)live(c->fl, c->s, c->cs, c->v, c->cv, c->ic);
}

#define MAXCASE 26
#define HMAX    4200
#define NMAX    64
static wchar_t  hbuf[MAXCASE][HMAX];
static wchar_t  nbuf[MAXCASE][NMAX];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static unsigned long long rs = 0x13579BDF2468ACE0ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

/* fill: 0 = 26 letters, 1 = a run of 'a', 2 = Cyrillic */
static void add(const char* label, DWORD fl, int n, int m, int ic, int fill, int plant, int minus1)
{
    int k = nc, i;
    wchar_t* h = hbuf[k];
    wchar_t* v = nbuf[k];
    for (i = 0; i < n; ++i)
        h[i] = (fill == 0) ? (wchar_t)(L'a' + (rnd() % 26))
             : (fill == 1) ? L'a'
                           : (wchar_t)(0x0430 + (rnd() % 32));
    h[n] = 0;
    for (i = 0; i < m; ++i)
        v[i] = (fill == 0) ? (wchar_t)(L'A' + (rnd() % 26))            /* absent when fill=0 */
             : (fill == 1) ? ((i == 0 || i == m - 1) ? L'a' : L'z')    /* BOTH anchors hit */
                           : (wchar_t)(0x0410 + (rnd() % 32));         /* Cyrillic UPPER */
    v[m] = 0;
    if (plant >= 0) for (i = 0; i < m; ++i) h[plant + i] = v[i];
    ctxs[k].fl = fl; ctxs[k].s = h; ctxs[k].v = v; ctxs[k].ic = (BOOL)ic;
    ctxs[k].cs = minus1 ? -1 : n;
    ctxs[k].cv = minus1 ? -1 : m;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)n * 2;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    int i, bad = 0;
    if (!k) k = LoadLibraryW(L"kernelbase.dll");
    live = (FFSO)GetProcAddress(k, "FindStringOrdinal");
    if (!live) { printf("FindStringOrdinal not found\n"); return 1; }
    if (wia_casemate_init() != 2) { printf("case classes larger than two\n"); return 1; }

    /*   label                             flags          n     m  ic fill plant  -1 */
    add("FROMSTART 16, miss",              F_FROMSTART,  16,   4, 0, 0,  -1, 0);
    add("FROMSTART 256, miss",             F_FROMSTART, 256,   8, 0, 0,  -1, 0);
    add("FROMSTART 4000, miss",            F_FROMSTART, 4000,  8, 0, 0,  -1, 0);
    add("FROMSTART 4000, hit @3000",       F_FROMSTART, 4000,  8, 0, 0, 3000, 0);
    add("FROMSTART 4000, CI miss",         F_FROMSTART, 4000,  8, 1, 0,  -1, 0);
    add("FROMSTART 4000, cch=-1 miss",     F_FROMSTART, 4000,  8, 0, 0,  -1, 1);
    add("FROMEND 4000, miss",              F_FROMEND,   4000,  8, 0, 0,  -1, 0);
    add("FROMEND 4000, hit @3900",         F_FROMEND,   4000,  8, 0, 0, 3900, 0);
    add("FROMEND 4000, hit @100",          F_FROMEND,   4000,  8, 0, 0,  100, 0);
    add("FROMEND 4000, CI miss",           F_FROMEND,   4000,  8, 1, 0,  -1, 0);
    add("FROMEND 256, miss",               F_FROMEND,    256,  8, 0, 0,  -1, 0);
    add("STARTSWITH 4000, yes",            F_STARTSWITH,4000,  8, 0, 0,    0, 0);
    add("STARTSWITH 4000, no",             F_STARTSWITH,4000,  8, 0, 0,  -1, 0);
    add("ENDSWITH 4000, yes",              F_ENDSWITH,  4000,  8, 0, 0, 3992, 0);
    add("ENDSWITH 4000, no",               F_ENDSWITH,  4000,  8, 0, 0,  -1, 0);
    add("ENDSWITH 4000, cch=-1 yes",       F_ENDSWITH,  4000,  8, 0, 0, 3992, 1);
    add("run of 'a', both anchors",        F_FROMSTART, 4000,  8, 0, 1,  -1, 0);
    add("run of 'a', FROMEND",             F_FROMEND,   4000,  8, 0, 1,  -1, 0);
    add("non-ASCII 4000, CI miss",         F_FROMSTART, 4000,  8, 1, 2,  -1, 0);
    add("non-ASCII 4000, CI FROMEND",      F_FROMEND,   4000,  8, 1, 2,  -1, 0);

    printf("== SUBJECTS (what each row actually measures) ==\n");
    printf("  %-30s %9s %6s %5s %3s  %8s %8s\n", "case", "flags", "n", "m", "ic", "ours", "live");
    for (i = 0; i < nc; ++i) {
        CTX* c = &ctxs[i];
        int a = wia_findstringordinal(c->fl, c->s, c->cs, c->v, c->cv, c->ic);
        int b = live(c->fl, c->s, c->cs, c->v, c->cv, c->ic);
        printf("  %-30s %9lX %6d %5d %3d  %8d %8d%s\n", cases[i].label, c->fl,
               c->cs, c->cv, (int)c->ic, a, b, (a == b) ? "" : "   <== DISAGREE");
        if (a != b) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }
    printf("  (a row labelled \"miss\" whose index is not -1 would be measuring a short scan)\n");

    return wia_bench_compare("kernelbase!FindStringOrdinal", cases, nc, 25);
}
