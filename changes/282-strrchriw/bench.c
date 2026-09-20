/* changes/282-strrchriw/bench.c
 *
 * Gate 2: time wia_strrchriw against the live shlwapi!StrRChrIW.
 *
 * The rows are the four dispatch shapes and the two ways a backward search ends.
 *
 * A backward search is not a forward search run in reverse when it comes to timing: the LAST match
 * is the one returned, so a match near the END is the cheap case and a match near the START is the
 * expensive one -- the opposite of change 281. Both are rows here, because a bench that only
 * planted matches at the end would measure the early-exit and report it as the function.
 *
 * The four needle shapes come from change 281's relation and take genuinely different paths: one
 * broadcast, four broadcasts, an inline list of up to eight, and an 8 KB membership bitmap.
 *
 * The pre-flight prints which path each row takes and what the live export returns, and stops the
 * bench if a row does not do what its name says. Change 269 shipped a row that measured a refusal
 * and called it a 30x win; change 281's first bench had FOUR rows whose names were wrong, including
 * one that claimed a miss while the haystack contained the needle.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef PCWSTR (WINAPI *F_rchr)(PCWSTR, PCWSTR, WCHAR);

extern const wchar_t* wia_strrchriw(const wchar_t*, const wchar_t*, wchar_t);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];

static F_rchr sys;

typedef struct { const wchar_t* s; const wchar_t* e; wchar_t needle; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += (uint64_t)(uintptr_t)wia_strrchriw(m->s, m->e, m->needle);
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += (uint64_t)(uintptr_t)sys(m->s, m->e, m->needle);
    return acc;
}

enum { K = 11 };

static wchar_t sMiss[512], sHitEnd[512], sHitStart[512], s4000[4001], s8[9];

int main(void)
{
    static const wchar_t* S[K];
    static const wchar_t* E[K];
    static wchar_t N[K];
    static const char* NAME[K] = {
        "511, MISS, 2-4 partners",
        "511, hit at 509 (last, cheap)",
        "511, hit at 1 (must scan it all)",
        "8, MISS",
        "8, hit at 7",
        "511, MISS, needle matches only itself",
        "511, MISS, 5-8 partners (KELVIN SIGN)",
        "511, MISS, >8 partners (soft hyphen)",
        "4000, MISS",
        "4000, hit at 1 (must scan it all)",
        "511, MISS, unaligned range"
    };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;
    unsigned selfonly = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_rchr)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrRChrIW");
    if (!sys) { printf("no StrRChrIW\n"); return 1; }
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }

    for (i = 0; i < 511; ++i) {
        sMiss[i] = (wchar_t)(L'a' + (i % 8));
        sHitEnd[i] = sMiss[i];
        sHitStart[i] = sMiss[i];
    }
    sHitEnd[509] = L'Z';
    sHitStart[1] = L'Z';
    for (i = 0; i < 4000; ++i) s4000[i] = (wchar_t)(L'a' + (i % 8));
    s4000[1] = L'Z';
    for (i = 0; i < 8; ++i) s8[i] = (wchar_t)(L'a' + i);

    for (i = 0x3000; i < 0xFFFF; ++i)
        if (wia_sci_n[i] == 0) { selfonly = (unsigned)i; break; }

    N[0] = L'#'; N[1] = L'Z'; N[2] = L'Z'; N[3] = L'#'; N[4] = L'h';
    N[5] = (wchar_t)selfonly; N[6] = 0x212A; N[7] = 0x00AD;
    N[8] = L'#'; N[9] = L'Z'; N[10] = L'#';

    S[0] = sMiss;     E[0] = sMiss + 511;
    S[1] = sHitEnd;   E[1] = sHitEnd + 511;
    S[2] = sHitStart; E[2] = sHitStart + 511;
    S[3] = s8;        E[3] = s8 + 8;
    S[4] = s8;        E[4] = s8 + 8;
    S[5] = sMiss;     E[5] = sMiss + 511;
    S[6] = sMiss;     E[6] = sMiss + 511;
    S[7] = sMiss;     E[7] = sMiss + 511;
    S[8] = s4000;     E[8] = s4000 + 4000;
    S[9] = s4000;     E[9] = s4000 + 4000;
    S[10] = sMiss + 1; E[10] = sMiss + 510;

    /* rows 8 and 9 share the buffer, so row 8 must not accidentally contain its needle */
    N[8] = L'#';

    printf("  pre-flight (which path each row takes, and what the LIVE export returns):\n");
    for (i = 0; i < K; ++i) {
        const wchar_t* r;
        unsigned n;
        cx[i].s = S[i]; cx[i].e = E[i]; cx[i].needle = N[i]; cx[i].reps = 1;
        n = wia_sci_n[(unsigned short)N[i]];
        r = sys(S[i], E[i], N[i]);
        printf("    %-42s needle U+%04X  partners %-3s  live %s",
               NAME[i], (unsigned)N[i],
               n == 0 ? "1" : (n == 255 ? ">8" : (n <= 4 ? "2-4" : "5-8")),
               r ? "HIT" : "miss");
        if (r) printf(" at %d", (int)(r - S[i]));
        printf("\n");
        {
            int wants_hit = (strstr(NAME[i], "hit") != 0);
            if ((r != 0) != wants_hit) { printf("      ^^ THIS ROW DOES NOT DO WHAT ITS NAME SAYS\n"); ++bad; }
        }
        cs[i].label = NAME[i];
        cs[i].bytes = (size_t)(E[i] - S[i]) * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (!selfonly || wia_sci_n[selfonly] != 0) { printf("    ^^ row 5 is not self-only\n"); ++bad; }
    if (wia_sci_n[0x212A] <= 4 || wia_sci_n[0x212A] > 8) { printf("    ^^ row 6 is not 5-8\n"); ++bad; }
    if (wia_sci_n[0x00AD] != 255) { printf("    ^^ row 7 is not a bitmap needle\n"); ++bad; }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("shlwapi StrRChrIW  (wia: change 281's per-needle match sets, scanned "
                             "BACKWARDS over an explicit range with both edge masks)", cs, K, 200);
}
