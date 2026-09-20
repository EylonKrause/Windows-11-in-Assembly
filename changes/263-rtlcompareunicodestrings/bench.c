/* changes/263-rtlcompareunicodestrings/bench.c
 *
 * OURS vs the LIVE ntdll!RtlCompareUnicodeStrings.
 *
 * What a comparison costs is how far it gets before it can answer, so every row states where its
 * answer comes from. Two strings that are equal are the expensive case -- the whole of both is
 * read -- and a pair differing in the first character is the cheap one, and a row that only
 * measured one of them would describe a different function from the one callers use.
 *
 * Four rows exist to attack this implementation specifically, because its case-insensitive path
 * compares the RAW characters first and folds only a block that disagrees:
 *
 *   equal, CI              the fold is never computed at all -- the best case for the design
 *   case-only difference   every block disagrees raw and every block has to be folded -- the worst
 *                          case, and the one that would expose a per-character table lookup
 *   non-ASCII, CI          the differing block leaves the in-vector fold and goes to the table,
 *                          which is the slow path kept for correctness rather than speed
 *   non-ASCII EQUAL, CI    ... and the same characters where nothing disagrees, so the table is
 *                          never reached even though the text is full of characters that need it
 *
 * A row that only tested ASCII would report the fast fold as if it were the whole story.
 *
 * The small rows call sixteen times per timed op, for the reason change 261 established by
 * measuring it: an empty call through this harness costs 2.32 ns, which is most of what a short
 * call measures, so a row that small compares the harness against itself. Their labels say so.
 *
 * Every row prints what it returned, and the subject table checks it against the live export before
 * anything is timed -- a comparison that answered from the wrong place would still produce a
 * plausible time.
 */
#include "bench.h"

typedef LONG (NTAPI *F_Cmp)(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);

LONG wia_compareunicodestrings(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);
void wia_upcase_init(void);
extern unsigned short wia_upcase[65536];

static F_Cmp live;

typedef struct { const wchar_t* a; const wchar_t* b; SIZE_T la, lb; int ci; ULONG reps; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i)
        acc += (uint64_t)(LONG_PTR)wia_compareunicodestrings(c->a, c->la, c->b, c->lb, (BOOLEAN)c->ci);
    return acc;
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i)
        acc += (uint64_t)(LONG_PTR)live(c->a, c->la, c->b, c->lb, (BOOLEAN)c->ci);
    return acc;
}

#define MAXCASE 24
#define CHARS   4096
static wchar_t  pa[MAXCASE][CHARS + 2], pb[MAXCASE][CHARS + 2];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

/* kind: 0 equal ASCII, 1 case-only difference, 2 differ at the front, 3 differ at the back,
         4 a prefix (unequal lengths), 5 equal but full of non-ASCII, 6 non-ASCII differing */
static void add(const char* label, int kind, SIZE_T len, int ci)
{
    int k = nc;
    SIZE_T i;
    wchar_t* a = pa[k];
    wchar_t* b = pb[k];
    SIZE_T lb = len;

    for (i = 0; i < len; ++i) {
        wchar_t c = (wchar_t)(L'a' + (i % 26));
        wchar_t hi = (wchar_t)(0x00E0 + (i % 24));           /* Latin-1 letters that fold */
        switch (kind) {
        case 0: a[i] = c;  b[i] = c;                                        break;
        case 1: a[i] = c;  b[i] = (wchar_t)(c - 32);                        break;
        case 2: a[i] = c;  b[i] = c;                                        break;
        case 3: a[i] = c;  b[i] = c;                                        break;
        case 4: a[i] = c;  b[i] = c;                                        break;
        case 5: a[i] = hi; b[i] = hi;                                       break;
        default: a[i] = hi; b[i] = (wchar_t)wia_upcase[hi];                 break;
        }
    }
    if (kind == 2 && len) b[0] = L'!';
    if (kind == 3 && len) b[len - 1] = L'!';
    if (kind == 4) lb = len / 2;                              /* a prefix: the lengths decide */

    ctxs[k].a = a; ctxs[k].b = b; ctxs[k].la = len; ctxs[k].lb = lb; ctxs[k].ci = ci;
    ctxs[k].reps = (len <= 32) ? 16 : 1;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)(len < lb ? len : lb) * 2 * ctxs[k].reps;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live = (F_Cmp)GetProcAddress(h, "RtlCompareUnicodeStrings");
    if (!live) { printf("RtlCompareUnicodeStrings not found\n"); return 1; }
    wia_upcase_init();

    /*   label                                          kind   len   ci */
    add("EQUAL 4000 ch, case-sensitive",                    0, 4000, 0);
    add("EQUAL 4000 ch, case-INSENSITIVE",                  0, 4000, 1);
    add("CASE-ONLY difference 4000 ch, CI (every block folds)", 1, 4000, 1);
    add("CASE-ONLY difference 4000 ch, case-sensitive",     1, 4000, 0);
    add("EQUAL 4000 ch NON-ASCII, CI (fold never reached)", 5, 4000, 1);
    add("NON-ASCII vs its upcase, 4000 ch, CI (the table)", 6, 4000, 1);
    add("EQUAL 400 ch, case-sensitive",                     0,  400, 0);
    add("EQUAL 400 ch, CI",                                 0,  400, 1);
    add("differ at the FIRST character, 4000 ch",           2, 4000, 0);
    add("differ at the LAST character, 4000 ch",            3, 4000, 0);
    add("differ at the LAST character, 4000 ch, CI",        3, 4000, 1);
    add("a PREFIX: 4000 vs 2000, the lengths decide",       4, 4000, 0);
    add("EQUAL 32 ch (x16 calls)",                          0,   32, 0);
    add("EQUAL 32 ch, CI (x16 calls)",                      0,   32, 1);
    add("EQUAL 16 ch (x16 calls)",                          0,   16, 0);
    add("EQUAL 16 ch, CI (x16 calls)",                      0,   16, 1);
    add("EQUAL 8 ch, below one block (x16 calls)",          0,    8, 0);
    add("EQUAL 8 ch, CI, below one block (x16 calls)",      0,    8, 1);
    add("CASE-ONLY difference 8 ch, CI (x16 calls)",        1,    8, 1);
    add("differ at the FIRST character, 8 ch (x16 calls)",  2,    8, 0);

    printf("== SUBJECTS (what each row actually answers) ==\n");
    printf("  %-52s %6s %6s %3s  %10s %10s\n", "case", "len1", "len2", "CI", "ours", "live");
    for (i = 0; i < nc; ++i) {
        LONG ro = wia_compareunicodestrings(ctxs[i].a, ctxs[i].la, ctxs[i].b, ctxs[i].lb,
                                            (BOOLEAN)ctxs[i].ci);
        LONG rl = live(ctxs[i].a, ctxs[i].la, ctxs[i].b, ctxs[i].lb, (BOOLEAN)ctxs[i].ci);
        printf("  %-52s %6Iu %6Iu %3s  %10ld %10ld%s\n", cases[i].label, ctxs[i].la, ctxs[i].lb,
               ctxs[i].ci ? "yes" : "no", ro, rl, ro == rl ? "" : "   <== DISAGREE");
        if (ro != rl) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlCompareUnicodeStrings", cases, nc, 25);
}
