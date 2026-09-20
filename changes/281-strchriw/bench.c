/* changes/281-strchriw/bench.c
 *
 * Gate 2: time wia_strchriw against the live shlwapi!StrChrIW.
 *
 * The rows are the three dispatch paths and the two ways a search ends, because those are what the
 * implementation distinguishes:
 *
 *   * a needle that matches only ITSELF (56825 of the 65535) -- one broadcast, one compare per 16;
 *   * a needle with 2..4 partners -- four broadcasts, four compares. Every ASCII letter is here:
 *     'a' matches A, a, U+1D2C and U+1D43;
 *   * a needle with 5..8 partners -- the inline list. U+212A KELVIN SIGN matches K, k, U+1D37,
 *     U+1D4F and itself, which is five;
 *   * a needle with MORE than eight -- the bitmap. U+00AD heads the 3237 ignorables;
 *   * and a MISS, which must scan the whole string, against a HIT, which stops early. The shipped
 *     export costs 43 ns per character, so the two differ by more than an order of magnitude in it.
 *
 * A bench of one path would measure a quarter of the function. The pre-flight below prints which
 * path each row actually takes, from the same table impl.asm dispatches on, and stops the bench if
 * a row does not reach the path its name promises -- change 269 shipped a row that measured a
 * refusal and reported it as a 30x win, and change 279's "no room" row was not a refusal at all
 * until its pre-flight said so.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);

extern const wchar_t* wia_strchriw(const wchar_t*, wchar_t);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];

static F_chr sys;

typedef struct { const wchar_t* s; wchar_t needle; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += (uint64_t)(uintptr_t)wia_strchriw(m->s, m->needle);
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += (uint64_t)(uintptr_t)sys(m->s, m->needle);
    return acc;
}

enum { K = 12 };

/* Every haystack is built from 'a'..'h' only, so that the needles chosen to exercise the other
 * dispatch paths -- kelvin sign, the soft hyphen, a self-only code unit -- genuinely miss. The
 * first version of this file used 'a'..'x', which contains 'k', and the pre-flight caught the
 * KELVIN row reporting a hit at offset 10 while its name said MISS. That is the same failure
 * change 269 shipped: a row that measures something other than what it is called. */
static wchar_t sMiss[512], sHit[512], sHit1[512], s4000[4001], sHit4000[4001], s8[9];

int main(void)
{
    static const wchar_t* S[K];
    static wchar_t N[K];
    static const char* NAME[K] = {
        "511 chars, MISS, 2-4 partners",
        "511 chars, hit at 400, 2-4 partners",
        "511 chars, hit at 1 (immediate)",
        "8 chars, MISS",
        "8 chars, hit",
        "511 chars, MISS, needle matches only itself",
        "511 chars, MISS, 5-8 partners (KELVIN SIGN)",
        "511 chars, MISS, >8 partners (soft hyphen)",
        "4000 chars, MISS",
        "4000 chars, hit at 3900",
        "511 chars, MISS, unaligned start",
        "511 chars, MISS, uppercase needle"
    };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;
    unsigned selfonly = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (F_chr)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrChrIW");
    if (!sys) { printf("no StrChrIW\n"); return 1; }
    if (wia_sci_init()) { printf("the tables disagree with the live export\n"); return 1; }

    for (i = 0; i < 511; ++i) {
        sMiss[i] = (wchar_t)(L'a' + (i % 8));
        sHit[i]  = sMiss[i];
        sHit1[i] = sMiss[i];
    }
    sMiss[511] = sHit[511] = sHit1[511] = 0;
    sHit[400] = L'Z';
    sHit1[1]  = L'Z';
    for (i = 0; i < 4000; ++i) { s4000[i] = (wchar_t)(L'a' + (i % 8)); sHit4000[i] = s4000[i]; }
    s4000[4000] = sHit4000[4000] = 0;
    sHit4000[3900] = L'Z';
    for (i = 0; i < 8; ++i) s8[i] = (wchar_t)(L'a' + i);
    s8[8] = 0;

    /* a needle that matches only ITSELF, found from the same table impl.asm dispatches on rather
       than guessed at -- the first version guessed U+4E00 and the pre-flight rejected it */
    for (i = 0x3000; i < 0xFFFF; ++i)
        if (wia_sci_n[i] == 0) { selfonly = (unsigned)i; break; }

    N[0] = L'#';  N[1] = L'Z';  N[2] = L'Z';  N[3] = L'#';  N[4] = L'c';
    N[5] = (wchar_t)selfonly;   N[6] = 0x212A; N[7] = 0x00AD;
    N[8] = L'#';  N[9] = L'Z';  N[10] = L'#'; N[11] = L'Q';

    S[0] = sMiss; S[1] = sHit;  S[2] = sHit1; S[3] = s8;   S[4] = s8;
    S[5] = sMiss; S[6] = sMiss; S[7] = sMiss; S[8] = s4000;
    S[9] = sHit4000; S[10] = sMiss + 1; S[11] = sMiss;

    printf("  pre-flight (which path each row takes, and what the LIVE export returns):\n");
    for (i = 0; i < K; ++i) {
        const wchar_t* r;
        unsigned n;
        cx[i].s = S[i]; cx[i].needle = N[i]; cx[i].reps = 1;
        n = wia_sci_n[(unsigned short)N[i]];
        r = sys(S[i], N[i]);
        printf("    %-46s needle U+%04X  partners %-3s  live %s",
               NAME[i], (unsigned)N[i],
               n == 0 ? "1" : (n == 255 ? ">8" : (n <= 4 ? "2-4" : "5-8")),
               r ? "HIT" : "miss");
        if (r) printf(" at %d", (int)(r - S[i]));
        printf("\n");
        {
            int wants_hit = (strstr(NAME[i], "hit") != 0);
            if ((r != 0) != wants_hit) {
                printf("      ^^ THIS ROW DOES NOT DO WHAT ITS NAME SAYS\n");
                ++bad;
            }
        }
        cs[i].label = NAME[i];
        cs[i].bytes = (size_t)wcslen(S[i]) * 2;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (!selfonly)                { printf("    ^^ no self-only needle found\n"); ++bad; }
    if (wia_sci_n[selfonly] != 0) { printf("    ^^ row 5 is not a self-only needle\n"); ++bad; }
    if (wia_sci_n[0x212A] <= 4 || wia_sci_n[0x212A] > 8)
                                  { printf("    ^^ row 6 is not a 5-8 partner needle\n"); ++bad; }
    if (wia_sci_n[0x00AD] != 255) { printf("    ^^ row 7 is not a bitmap needle\n"); ++bad; }
    if (wia_sci_n[L'Z'] == 0 || wia_sci_n[L'Z'] > 4)
                                  { printf("    ^^ row 1 is not a 2-4 partner needle\n"); ++bad; }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("shlwapi StrChrIW  (wia: per-needle match sets; AVX2 for <=4 partners, "
                             "inline list for 5-8, membership bitmap beyond)", cs, K, 200);
}
