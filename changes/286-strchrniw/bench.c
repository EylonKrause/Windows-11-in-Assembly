/* changes/286-strchrniw/bench.c
 *
 * Gate 2: time wia_strchrniw against the live shlwapi!StrChrNIW.
 *
 * THIS BENCH ALSO ESTABLISHES WHAT THE EXPORT ACTUALLY COSTS. The only prior number for it,
 * discovery/charclass_strcmp_2026.c's 1655 ns over 511 code units, was taken through the wrong
 * prototype: it called (start, start+511, '#') on a function whose real shape is (start, match, count),
 * so it passed the low half of an address as the character and 35 as the count. That does not fault, so
 * it produced a number for a different question. Every row below uses the measured prototype.
 *
 * THE ROWS ARE WHAT A COUNT-BOUNDED CHARACTER SEARCH COSTS:
 *
 *   * a MISS over 511 code units with the count covering all of them -- the worst case;
 *   * a hit near the START, which is where such a search usually stops;
 *   * a hit near the END;
 *   * a SMALL count over a long string, which is the whole point of this export over StrChrIW: the
 *     count, not the string, must decide how much work happens;
 *   * a count that reaches far PAST the terminator, so the terminator has to stop the scan;
 *   * a character with more than four partners, which takes the scalar WIDE path;
 *   * short strings, where the whole cost is the call and the set dispatch.
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

typedef PWSTR (WINAPI *FCN)(PCWSTR, WCHAR, UINT);

extern const wchar_t* wia_strchrniw(const wchar_t*, wchar_t, unsigned);
int wia_sci_init(void);
extern const unsigned char wia_sci_n[];

static FCN sys;

typedef struct { const wchar_t* s; wchar_t m; unsigned cch; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* x = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < x->reps; ++i) acc += (uint64_t)(uintptr_t)wia_strchrniw(x->s, x->m, x->cch);
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* x = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < x->reps; ++i) acc += (uint64_t)(uintptr_t)sys(x->s, x->m, x->cch);
    return acc;
}

enum { K = 8 };

static wchar_t h511[512], h511s[512], h511e[512], hshort[512], h16[17];

int main(void)
{
    static const wchar_t* S[K];
    static wchar_t M[K];
    static unsigned C[K];
    static const char* NAME[K] = {
        "511, MISS, count 511",
        "511, hit near the START (3)",
        "511, hit near the END (508)",
        "511 buffer, MISS, count only 16",
        "terminated at 64, MISS, count 4 billion",
        "511, MISS, character with MANY partners (WIDE path)",
        "16, MISS, count 16",
        "16, hit at 12"
    };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;
    unsigned widechar = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FCN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrChrNIW");
    if (!sys) { printf("no StrChrNIW\n"); return 1; }
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }

    for (i = 1; i < 0xFFFF; ++i) if (wia_sci_n[i] == 255) { widechar = (unsigned)i; break; }

    for (i = 0; i < 511; ++i) {
        h511[i]   = (wchar_t)(L'a' + (i % 5));
        h511s[i]  = h511[i];
        h511e[i]  = h511[i];
        hshort[i] = h511[i];
    }
    h511[511] = h511s[511] = h511e[511] = hshort[511] = 0;
    h511s[3]   = L'q';
    h511e[508] = L'q';
    hshort[64] = 0;                     /* the same buffer, terminated early */
    for (i = 0; i < 16; ++i) h16[i] = (wchar_t)(L'a' + (i % 5));
    h16[16] = 0;
    h16[12] = L'q';

    S[0] = h511;   M[0] = L'Q';               C[0] = 511;
    S[1] = h511s;  M[1] = L'Q';               C[1] = 511;
    S[2] = h511e;  M[2] = L'Q';               C[2] = 511;
    S[3] = h511;   M[3] = L'Q';               C[3] = 16;
    S[4] = hshort; M[4] = L'Q';               C[4] = 0xFFFFFFFFu;
    S[5] = h511;   M[5] = (wchar_t)widechar;  C[5] = 511;
    S[6] = h16;    M[6] = L'Z';               C[6] = 16;
    S[7] = h16;    M[7] = L'Q';               C[7] = 16;

    printf("  pre-flight (what the LIVE export returns for each row; the prototype is\n"
           "  (start, match, count), settled by probes/contract.c):\n");
    for (i = 0; i < K; ++i) {
        const void* r;
        cx[i].s = S[i]; cx[i].m = M[i]; cx[i].cch = C[i]; cx[i].reps = 1;
        r = sys(S[i], M[i], C[i]);
        printf("    %-52s U+%04X count %-10u live %s", NAME[i], (unsigned)M[i], C[i],
               r ? "HIT" : "miss");
        if (r) printf(" at %d", (int)((const wchar_t*)r - S[i]));
        printf("\n");
        {
            int wants_hit = (strstr(NAME[i], "hit ") != 0);
            if ((r != 0) != wants_hit) { printf("      ^^ THIS ROW DOES NOT DO WHAT ITS NAME SAYS\n"); ++bad; }
        }
        cs[i].label = NAME[i];
        cs[i].bytes = (size_t)wcslen(S[i]) * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("shlwapi StrChrNIW  (wia: change 281's per-character match set broadcast, "
                             "forward scan with the terminator folded in and the count as the bound)",
                             cs, K, 200);
}
