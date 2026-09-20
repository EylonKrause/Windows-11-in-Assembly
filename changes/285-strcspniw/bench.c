/* changes/285-strcspniw/bench.c
 *
 * Gate 2: time wia_strcspniw against the live shlwapi!StrCSpnIW.
 *
 * The rows are what a set span actually costs, and the set is as much of the input as the string:
 *
 *   * no match over a long string, which must reach the terminator -- the worst case for the scan;
 *   * a match near the START, which is where a span usually stops in real use (a delimiter search);
 *   * a match near the END of a long string;
 *   * a ONE-character set, which expands to at most four accept entries and therefore takes exactly
 *     one vector pass -- the fast path;
 *   * a set large enough to need SEVERAL chunks, so the per-chunk pass and its shrinking bound are
 *     both measured;
 *   * a set holding a 255-sentinel member (an ignorable), which forces the SCALAR path -- the row that
 *     stops the headline number from being the easy case only;
 *   * short strings, where the whole cost is the call and the set expansion.
 *
 * The pre-flight prints what the live export returns for each row and stops the bench if a row does not
 * do what its name says. Change 281's first bench had four such rows, and change 279's had one.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef int (WINAPI *FSPN)(PCWSTR, PCWSTR);

extern int wia_strcspniw(const wchar_t*, const wchar_t*);
int wia_sci_init(void);

static FSPN sys;

typedef struct { const wchar_t* s; const wchar_t* set; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += (uint64_t)(unsigned)wia_strcspniw(m->s, m->set);
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += (uint64_t)(unsigned)sys(m->s, m->set);
    return acc;
}

enum { K = 8 };

static wchar_t h511[512], h511s[512], h511e[512], h16[17];
static wchar_t setbig[13], setign[4];

int main(void)
{
    static const wchar_t* S[K];
    static const wchar_t* SET[K];
    static const char* NAME[K] = {
        "511, no match, 1-character set",
        "511, match near the START (3)",
        "511, match near the END (508)",
        "511, no match, 12-character set (several chunks)",
        "511, no match, set holds an IGNORABLE (scalar path)",
        "511, match near the START, 12-character set",
        "16, no match, 1-character set",
        "16, match at 12"
    };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FSPN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrCSpnIW");
    if (!sys) { printf("no StrCSpnIW\n"); return 1; }
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }

    for (i = 0; i < 511; ++i) {
        h511[i]  = (wchar_t)(L'a' + (i % 5));        /* a..e only */
        h511s[i] = h511[i];
        h511e[i] = h511[i];
    }
    h511[511] = h511s[511] = h511e[511] = 0;
    h511s[3]   = L'q';
    h511e[508] = L'q';
    for (i = 0; i < 16; ++i) h16[i] = (wchar_t)(L'a' + (i % 5));
    h16[16] = 0;
    h16[12] = L'q';

    /* a 12-character set of code units that do NOT occur in the strings above */
    for (i = 0; i < 12; ++i) setbig[i] = (wchar_t)(L'M' + i);
    setbig[12] = 0;
    /* a set whose single member carries the 255 bitmap sentinel: the scalar path */
    setign[0] = 0x00AD; setign[1] = 0;

    S[0] = h511;  SET[0] = L"Q";
    S[1] = h511s; SET[1] = L"Q";
    S[2] = h511e; SET[2] = L"Q";
    S[3] = h511;  SET[3] = setbig;
    S[4] = h511;  SET[4] = setign;
    S[5] = h511s; SET[5] = setbig;
    S[6] = h16;   SET[6] = L"Z";
    S[7] = h16;   SET[7] = L"Q";

    printf("  pre-flight (what the LIVE export returns for each row):\n");
    for (i = 0; i < K; ++i) {
        int r, len = (int)wcslen(S[i]);
        cx[i].s = S[i]; cx[i].set = SET[i]; cx[i].reps = 1;
        r = sys(S[i], SET[i]);
        printf("    %-52s set len %2d  live %d of %d%s\n", NAME[i], (int)wcslen(SET[i]), r, len,
               r == len ? "  (no match)" : "");
        {
            int wants_match = (strstr(NAME[i], "match near") != 0) || (strstr(NAME[i], "match at") != 0);
            int got_match = (r != len);
            if (got_match != wants_match) {
                printf("      ^^ THIS ROW DOES NOT DO WHAT ITS NAME SAYS\n"); ++bad;
            }
        }
        cs[i].label = NAME[i];
        cs[i].bytes = (size_t)len * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("shlwapi StrCSpnIW  (wia: change 281's per-member match sets expanded "
                             "into an accept list, scanned four at a time with the terminator folded in)",
                             cs, K, 200);
}
