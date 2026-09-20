/* discovery/cmpordinal_scalar_reentry.c
 *
 * tools/vector-reentry-audit.py flags exactly one site in this repository as needing a human:
 *
 *     210-comparestringordinal   impl.asm:303   jmp ci_next
 *
 * The rule it screens for is change 263's: a scalar walk must not re-enter a vector loop. The shape
 * is bit-exact, invisible to a correctness gate, and invisible to a benchmark built from the input
 * the fast path handles, on the input it does NOT handle, every scalar character pays for the
 * vector probe again.
 *
 * Reading the site, it is the real shape. `ci_next` is the loop head: two 32-byte loads, a raw
 * compare (tier 1), and an all-ASCII test that folds in-register (tier 2). The scalar path at 303
 * does `inc r10d ; jmp ci_next`, so a character handled scalar-ly re-runs both tiers before the
 * next one is looked at.
 *
 * When does that actually happen? Not for ASCII. Two ASCII chunks that differ only in case are
 * folded and compared entirely in tier 2 and advance sixteen characters at a time. The scalar walk
 * is reached only when a chunk is NOT all-ASCII and the fold has to go through the table, which
 * is to say, for any text that is not English. That is a narrower case than "case-insensitive
 * compare" but a much wider one than "exotic".
 *
 * So this measures the three inputs side by side, per character:
 *
 *     1. ASCII, differing case, tier 2, sixteen characters per probe
 *     2. non-ASCII, EQUAL raw, tier 1, sixteen characters per probe
 *     3. non-ASCII, differing case, the scalar walk, and the suspected one per probe
 *
 * If row 3 costs roughly what rows 1 and 2 cost per character, the re-entry is not hurting and the
 * audit's flag can be closed with a measurement. If it costs several times more AND grows against
 * the live export's own cost for the same input, it is change 263's defect in a landed change, and
 * the fix is the one 263 used: let the scalar walk run to the end of its run before going back.
 *
 * The live export is timed on the identical input in every row, because the question is not "is
 * the scalar path slow"; it is "are we slower THAN WINDOWS on this input", which is the only
 * thing that decides whether the change still earns its place.
 *
 * Run on an idle machine, not during a revalidation sweep. min-of-N.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern int  wia_comparestringordinal(const wchar_t*, int, const wchar_t*, int, int);
extern void wia_upcase_init(void);   /* the fold table is built from the OS; without
                                        this call the table is zeros and the scalar
                                        path would be measured doing nothing. */

typedef int (WINAPI *F_cso)(LPCWCH, int, LPCWCH, int, BOOL);
static F_cso sys_cso;

static double bestns(void (*op)(void), int inner, int trials)
{
    LARGE_INTEGER f, a, b;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 64; ++i) op();
    for (t = 0; t < trials; ++t) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) op();
        QueryPerformanceCounter(&b);
        {
            double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            if (v < bv) bv = v;
        }
    }
    return bv;
}

static volatile unsigned long long sink;

#define CAP 4200
static wchar_t A[CAP], B[CAP];
static int g_len;

static void o_ours(void){ sink += (unsigned)wia_comparestringordinal(A, g_len, B, g_len, 1); }
static void o_sys (void){ sink += (unsigned)sys_cso(A, g_len, B, g_len, TRUE); }

/* shape 0: ASCII differing case   1: non-ASCII equal raw   2: non-ASCII differing case */
static void fill(int shape, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        switch (shape) {
        case 0:
            A[i] = (wchar_t)(L'A' + (i % 26));
            B[i] = (wchar_t)(L'a' + (i % 26));
            break;
        case 1:
            A[i] = (wchar_t)(0x0410 + (i % 32));      /* Cyrillic capital */
            B[i] = A[i];
            break;
        default:
            A[i] = (wchar_t)(0x0410 + (i % 32));      /* Cyrillic capital  A..  */
            B[i] = (wchar_t)(0x0430 + (i % 32));      /* Cyrillic small    a..  -- folds equal */
            break;
        }
    }
    A[n] = 0; B[n] = 0;
}

int main(void)
{
    static const char* NAME[3] = { "ASCII, differing case (tier 2)",
                                   "non-ASCII, equal raw  (tier 1)",
                                   "non-ASCII, diff case  (SCALAR)" };
    static const int LENS[] = { 64, 256, 1024, 4000 };
    HMODULE h = LoadLibraryW(L"kernel32.dll");
    int li, shape;

    wia_upcase_init();
    sys_cso = (F_cso)GetProcAddress(h, "CompareStringOrdinal");
    if (!sys_cso) { printf("no CompareStringOrdinal\n"); return 2; }

    printf("Change 210's scalar walk re-enters its vector loop head. This measures when that costs.\n");
    printf("RUN ON AN IDLE MACHINE. min-of-30. Every row states the verdict both sides returned.\n\n");

    for (li = 0; li < 4; ++li) {
        g_len = LENS[li];
        printf("---- %d characters ----\n", g_len);
        printf("  %-32s %11s %11s %9s %11s  %s\n",
               "input", "ours ns", "system ns", "ratio", "ours ns/ch", "returned");
        for (shape = 0; shape < 3; ++shape) {
            double o, s;
            int r1, r2;
            fill(shape, g_len);
            r1 = wia_comparestringordinal(A, g_len, B, g_len, 1);
            r2 = sys_cso(A, g_len, B, g_len, TRUE);
            o = bestns(o_ours, 1000, 30);
            s = bestns(o_sys,  1000, 30);
            printf("  %-32s %11.2f %11.2f %8.2fx %11.4f  ours=%d sys=%d%s\n",
                   NAME[shape], o, s, (o > 0 ? s / o : 0.0), o / (double)g_len, r1, r2,
                   (r1 != r2) ? "   *** DISAGREE ***" : "");
        }
        printf("\n");
    }

    printf("HOW TO READ THIS. Compare the 'ours ns/ch' column DOWN each block. Rows 1 and 2 advance\n"
           "sixteen characters per vector probe; row 3 is the suspected one-per-probe. If row 3's\n"
           "per-character cost is close to theirs, the re-entry is not hurting and the audit's flag\n"
           "closes with a measurement. If it is several times theirs AND the ratio column drops\n"
           "below 1, change 210 is slower than Windows on any text that is not English, which is\n"
           "change 263's defect in a landed change and is fixed the way 263 fixed it: let the scalar\n"
           "walk run to the end of its run before returning to the vector head.\n");
    return 0;
}
