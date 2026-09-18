/* changes/283-strrstriw/bench.c
 *
 * Gate 2: time wia_strrstriw against the live shlwapi!StrRStrIW.
 *
 * THE ROWS ARE WHAT A BACKWARD SUBSTRING SEARCH ACTUALLY COSTS:
 *
 *   * a MISS over a long haystack, which must try every start position -- the worst case;
 *   * a hit near the END, which a backward search finds at once and a forward one does not;
 *   * a hit near the START, which is the backward search's own worst case;
 *   * a needle whose first character is COMMON, so the filter fires constantly and the verification
 *     runs on almost every position -- this is the row that measures the filter's value, and a
 *     bench without it would report only the easy case;
 *   * short haystacks, where the whole cost is the call and the two length scans.
 *
 * The pre-flight prints what the live export returns for each row and stops the bench if a row does
 * not do what its name says. Change 281's first bench had four such rows.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);

extern const wchar_t* wia_strrstriw(const wchar_t*, const wchar_t*, const wchar_t*);
int wia_sci_init(void);

static F3 sys;

typedef struct { const wchar_t* s; const wchar_t* e; const wchar_t* n; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += (uint64_t)(uintptr_t)wia_strrstriw(m->s, m->e, m->n);
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += (uint64_t)(uintptr_t)sys(m->s, m->e, m->n);
    return acc;
}

enum { K = 8 };

static wchar_t h511[512], h511e[512], h511s[512], hcommon[512], h16[17];

int main(void)
{
    static const wchar_t* S[K];
    static const wchar_t* E[K];
    static const wchar_t* N[K];
    static const char* NAME[K] = {
        "511, MISS, needle length 4",
        "511, hit near the END (509)",
        "511, hit near the START (1)",
        "511, MISS, needle first char COMMON",
        "511, MISS, needle length 1",
        "16, MISS",
        "16, hit at 12",
        "511, MISS, `end` past the terminator"
    };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F3)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrRStrIW");
    if (!sys) { printf("no StrRStrIW\n"); return 1; }
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }

    for (i = 0; i < 511; ++i) {
        h511[i]    = (wchar_t)(L'a' + (i % 8));
        h511e[i]   = h511[i];
        h511s[i]   = h511[i];
        hcommon[i] = L'q';                       /* every position starts with the filter char */
    }
    h511[511] = h511e[511] = h511s[511] = hcommon[511] = 0;
    h511e[509] = L'Z'; h511e[510] = L'Z';
    h511s[1]   = L'Z'; h511s[2]   = L'Z';
    for (i = 0; i < 16; ++i) h16[i] = (wchar_t)(L'a' + i);
    h16[16] = 0;

    S[0] = h511;    E[0] = h511 + 511;    N[0] = L"WXYZ";
    S[1] = h511e;   E[1] = h511e + 511;   N[1] = L"zz";
    S[2] = h511s;   E[2] = h511s + 511;   N[2] = L"zz";
    S[3] = hcommon; E[3] = hcommon + 511; N[3] = L"QQQQZ";   /* 'Q' matches every character */
    S[4] = h511;    E[4] = h511 + 511;    N[4] = L"#";
    S[5] = h16;     E[5] = h16 + 16;      N[5] = L"#";
    S[6] = h16;     E[6] = h16 + 16;      N[6] = L"MN";
    S[7] = h511;    E[7] = h511 + 600;    N[7] = L"WXYZ";

    printf("  pre-flight (what the LIVE export returns for each row):\n");
    for (i = 0; i < K; ++i) {
        const wchar_t* r;
        cx[i].s = S[i]; cx[i].e = E[i]; cx[i].n = N[i]; cx[i].reps = 1;
        r = sys(S[i], E[i], N[i]);
        printf("    %-42s needle len %d  live %s", NAME[i], (int)wcslen(N[i]),
               r ? "HIT" : "miss");
        if (r) printf(" at %d", (int)(r - S[i]));
        printf("\n");
        {
            int wants_hit = (strstr(NAME[i], "hit") != 0);
            if ((r != 0) != wants_hit) { printf("      ^^ THIS ROW DOES NOT DO WHAT ITS NAME SAYS\n"); ++bad; }
        }
        cs[i].label = NAME[i];
        cs[i].bytes = (size_t)wcslen(S[i]) * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("shlwapi StrRStrIW  (wia: change 281's per-needle match sets, "
                             "backward candidate scan filtered on the needle's first character)",
                             cs, K, 200);
}
