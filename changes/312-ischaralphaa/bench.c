/* changes/312-ischaralphaa/bench.c
 * Gate 2: the four classifiers against live user32. A classifier is called a character at a time by
 * whatever is scanning text, so each timed op is 16 calls over 16 different bytes of a sample, through
 * a monomorphic call site per (function, side) -- change 304's call shape. Table ns are per 16 calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

typedef BOOL (WINAPI *PF)(CHAR);
extern BOOL wia_ischaralphaa(CHAR), wia_ischaralphanumerica(CHAR), wia_ischaruppera(CHAR), wia_ischarlowera(CHAR);
int wia_ica_init(void);

#define REP 16
#define SITE(N) static PF volatile p_##N; static __declspec(noinline) uint64_t op_##N(void* c) { const unsigned char* s = (const unsigned char*)c; uint64_t acc = 0; for (int j = 0; j < REP; ++j) acc += p_##N((CHAR)s[j]); return acc; }
SITE(oa) SITE(sa) SITE(on) SITE(sn) SITE(ou) SITE(su) SITE(ol) SITE(sl)

int main(void) {
    if (!wia_ica_init()) { printf("init failed\n"); return 3; }
    HMODULE u = LoadLibraryW(L"user32.dll");
    p_sa = (PF)GetProcAddress(u, "IsCharAlphaA"); p_sn = (PF)GetProcAddress(u, "IsCharAlphaNumericA");
    p_su = (PF)GetProcAddress(u, "IsCharUpperA"); p_sl = (PF)GetProcAddress(u, "IsCharLowerA");
    p_oa = wia_ischaralphaa; p_on = wia_ischaralphanumerica; p_ou = wia_ischaruppera; p_ol = wia_ischarlowera;
    static const unsigned char ascii[16] = "Hello, World 42!";
    static const unsigned char western[16] = { 'G', 0xE9, 'n', 0xE9, 'r', 'a', 'l', ' ', 0xC9, 't', 0xE9, ' ', 0xDF, '1', 0xFC, '.' };
    wia_op O[4] = { op_oa, op_on, op_ou, op_ol }, S[4] = { op_sa, op_sn, op_su, op_sl };
    const char* nm[4] = { "IsCharAlphaA", "IsCharAlphaNumericA", "IsCharUpperA", "IsCharLowerA" };
    static wia_case wc[8]; static char lab[8][48];
    for (int k = 0; k < 4; ++k)
        for (int t = 0; t < 2; ++t) {
            int i = k * 2 + t;
            sprintf(lab[i], "%s, %s", nm[k], t ? "Western" : "ASCII");
            wc[i].label = lab[i]; wc[i].bytes = 16; wc[i].ours = O[k]; wc[i].system = S[k];
            wc[i].ctx = (void*)(t ? western : ascii);
        }
    return wia_bench_compare("user32 IsCharAlphaA/AlphaNumericA/UpperA/LowerA  (wia: one byte-table load; ns per 16 calls)", wc, 8, 300);
}
