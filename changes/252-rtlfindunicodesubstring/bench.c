/* changes/252-rtlfindunicodesubstring/bench.c
 *
 * OURS vs the LIVE ntdll!RtlFindUnicodeSubstring on this machine.
 *
 * EVERY ROW STATES WHAT IT ACTUALLY DID before the table is printed -- the haystack length, the
 * needle length, the mode, and the offset all three agreed on. This project has twice measured a
 * no-op and believed it: a survey subject whose escapable characters sat in the wrong URL segment,
 * and a formatter whose digit table had never been built. A search benchmark is especially easy to
 * get wrong in that way, because a needle that is accidentally present at offset 0 turns a
 * full-scan row into a two-character row and still looks entirely plausible.
 *
 * THE ROWS ARE CHOSEN TO INCLUDE THE CASES THIS IMPLEMENTATION IS WORST AT, not only the ones it
 * is best at:
 *   * tiny inputs, where the vector loop can never run and the only question is whether the setup
 *     was skipped (it is -- see the `cmp r14d, 15` bypass in impl.asm);
 *   * a TWO-LETTER alphabet, where the two-anchor filter admits one position in four instead of
 *     one in a few hundred;
 *   * a RUN OF ONE CHARACTER with a needle whose first and last characters are both that
 *     character. With the far anchor fixed at m-1 this admits EVERY position and hands all of them
 *     to the scalar verifier; it measured 0.91x-1.17x, i.e. it crossed the 0.97x gate at random
 *     from run to run, which is what forced impl.asm to CHOOSE its far anchor instead of assuming
 *     it. The row is kept so the fix stays measured;
 *   * an all-non-ASCII haystack -- Cyrillic here, but it stands for Greek, Hebrew and Japanese
 *     equally. This row is the reason the insensitive filter was rewritten: the first version
 *     folded to ASCII and had to treat every non-ASCII unit as a candidate, which made this row
 *     2.33x while the ASCII rows were 16x. It is kept, and kept in this position, so that the
 *     rewrite is measured rather than asserted.
 */
#include "bench.h"
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef PWSTR (NTAPI *FFIND)(USTR*, USTR*, BOOLEAN);

PWSTR wia_findunicodesubstring(void* Full, void* Search, BOOLEAN CaseInSensitive);
int   wia_casemate_init(void);

static FFIND live;

typedef struct { USTR H, N; BOOLEAN ci; const char* note; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    return (uint64_t)(size_t)wia_findunicodesubstring(&c->H, &c->N, c->ci);
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    return (uint64_t)(size_t)live(&c->H, &c->N, c->ci);
}

/* ---- subject storage: each case gets its own haystack and needle ---- */
#define MAXCASE 20
#define HMAX    4096
#define NMAX    64
static wchar_t hbuf[MAXCASE][HMAX];
static wchar_t nbuf[MAXCASE][NMAX];
static CTX     ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static unsigned long long rs = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

/* fill: 0 = 26 letters, 1 = two letters, 2 = a run of one letter, 3 = non-ASCII */
static void add(const char* label, int n, int m, int ci, int fill, int plant_at, int upper_plant)
{
    int i, k = nc;
    wchar_t* h = hbuf[k];
    wchar_t* nb = nbuf[k];
    for (i = 0; i < n; ++i) {
        switch (fill) {
        case 0:  h[i] = (wchar_t)(L'a' + (rnd() % 26)); break;
        case 1:  h[i] = (wchar_t)(L'a' + (rnd() % 2));  break;
        case 2:  h[i] = L'a';                            break;
        default: h[i] = (wchar_t)(0x0430 + (rnd() % 32)); break;   /* Cyrillic lowercase */
        }
    }
    for (i = 0; i < m; ++i) {
        switch (fill) {
        case 0:  nb[i] = (wchar_t)(L'A' + (rnd() % 26)); break;  /* uppercase: absent when fill=0 */
        case 1:  nb[i] = (wchar_t)(L'c' + (rnd() % 2));  break;  /* outside {a,b}: absent */
        case 2:  nb[i] = (i == 0 || i == m - 1) ? L'a' : L'z';   /* BOTH anchors hit, body misses */
                 break;
        default: nb[i] = (wchar_t)(0x0410 + (rnd() % 32)); break; /* Cyrillic UPPERCASE */
        }
    }
    if (plant_at >= 0) {
        for (i = 0; i < m; ++i) {
            wchar_t c = nb[i];
            if (upper_plant) {
                if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
                else if (c >= 0x0410 && c <= 0x042F) c = (wchar_t)(c + 32);
            }
            h[plant_at + i] = c;
        }
    }
    ctxs[k].H.Buffer = h;  ctxs[k].H.Length = (USHORT)(n * 2);  ctxs[k].H.MaximumLength = (USHORT)(n * 2);
    ctxs[k].N.Buffer = nb; ctxs[k].N.Length = (USHORT)(m * 2);  ctxs[k].N.MaximumLength = (USHORT)(m * 2);
    ctxs[k].ci = (BOOLEAN)ci;
    ctxs[k].note = label;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)n * 2;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE hm = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live = (FFIND)GetProcAddress(hm, "RtlFindUnicodeSubstring");
    if (!live) { printf("RtlFindUnicodeSubstring not found\n"); return 1; }
    if (wia_casemate_init() != 2) {
        printf("case classes larger than two -- see correctness.c section 0\n");
        return 1;
    }

    /*     label                         n     m  ci fill plant upper */
    add("4 ch, miss",                    4,    2, 0, 0,  -1, 0);
    add("16 ch, miss",                  16,    4, 0, 0,  -1, 0);
    add("64 ch, miss",                  64,    8, 0, 0,  -1, 0);
    add("256 ch, miss",                256,    8, 0, 0,  -1, 0);
    add("1024 ch, miss",              1024,    8, 0, 0,  -1, 0);
    add("4000 ch, miss",              4000,    8, 0, 0,  -1, 0);
    add("4000 ch, hit @ 3000",        4000,    8, 0, 0, 3000, 0);
    add("4000 ch, m=1 miss",          4000,    1, 0, 0,  -1, 0);
    add("4000 ch, m=32 miss",         4000,   32, 0, 0,  -1, 0);
    add("16 ch, CI miss",               16,    4, 1, 0,  -1, 0);
    add("256 ch, CI miss",             256,    8, 1, 0,  -1, 0);
    add("4000 ch, CI miss",           4000,    8, 1, 0,  -1, 0);
    add("4000 ch, CI hit @ 3000",     4000,    8, 1, 0, 3000, 1);
    add("2-letter alpha, 4000",       4000,    8, 0, 1,  -1, 0);
    add("2-letter alpha, 4000 CI",    4000,    8, 1, 1,  -1, 0);
    add("run of 'a', both anchors",   4000,    8, 0, 2,  -1, 0);
    add("run of 'a', anchors, CI",    4000,    8, 1, 2,  -1, 0);
    add("non-ASCII 4000, CI miss",    4000,    8, 1, 3,  -1, 0);
    add("non-ASCII 4000, CI hit",     4000,    8, 1, 3, 3000, 1);

    /* ---- EVERY ROW STATES WHAT IT DID, and all three answers must agree before timing ---- */
    printf("== SUBJECTS (what each row actually measures) ==\n");
    printf("  %-28s %6s %4s %3s  %8s %8s\n", "case", "n", "m", "ci", "ours@", "live@");
    for (i = 0; i < nc; ++i) {
        PWSTR a = wia_findunicodesubstring(&ctxs[i].H, &ctxs[i].N, ctxs[i].ci);
        PWSTR b = live(&ctxs[i].H, &ctxs[i].N, ctxs[i].ci);
        int oa = a ? (int)(a - ctxs[i].H.Buffer) : -1;
        int ob = b ? (int)(b - ctxs[i].H.Buffer) : -1;
        printf("  %-28s %6d %4d %3d  %8d %8d%s\n", cases[i].label,
               ctxs[i].H.Length / 2, ctxs[i].N.Length / 2, ctxs[i].ci, oa, ob,
               (oa == ob) ? "" : "   <== DISAGREE");
        if (oa != ob) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }
    printf("  (a row labelled \"miss\" whose offset is not -1 would be measuring a short scan,\n"
           "   not a full one -- that is the mistake this block exists to make impossible)\n");

    return wia_bench_compare("ntdll!RtlFindUnicodeSubstring", cases, nc, 25);
}
