// changes/167-pathcommonprefixw/bench.c
// Gate 2: time wia_pathcommonprefixw against the live shlwapi!PathCommonPrefixW.
//
// THE ROW THIS CHANGE EXISTS FOR is "254 identical": discovery measured the shipped export at
// 698 ns on it -- 2.75 ns per character, the slowest of every shlwapi export this project had not
// yet converted, and the reason the target was picked at all.
//
// THE CASE MIX. The shipped function calls a comparison routine ONCE PER COMPONENT on top of two
// scalar scans per component, so its cost is driven by two things and the rows separate them:
//
//   * HOW FAR THE TWO PATHS AGREE, which is what both implementations actually walk. Identical
//     paths at 8, 16, 32, 64, 128 and 254 characters bracket the vector loop from "shorter than one
//     block" to "many blocks".
//   * HOW MANY COMPONENTS that agreement spans, because that is what the shipped one pays per unit
//     of work and this one does not. The "254, 1 component" row is the same length as the "254
//     identical" row with the separators removed.
//
// AND THREE ROWS THAT ARE NOT ABOUT THE HAPPY PATH:
//   * "differ at 0" -- the answer is 0 and neither implementation walks anything. If a vector
//     prologue cost anything, this is the row that would show it.
//   * "case-differing" -- every component differs only in case, so the raw comparison fails at the
//     first character of every component and the fold table is consulted on each. That is this
//     implementation's own worst case, and it is a row rather than a footnote.
//   * "UNC" -- both paths skip two characters before the walk begins.
//
// NOTHING TO RESTORE: both inputs are read-only and the output is a separate buffer that is never
// read back. The output buffers still ROTATE across eight slots, STAGGERED WITHIN THEIR PAGES --
// change 250's benchmark reported a 0.87x regression that turned out to be 4K aliasing, because
// every slot began at page offset 0 and every store in every row fell in one L1 set.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int  wia_pathcommonprefixw(const wchar_t*, const wchar_t*, wchar_t*);
extern void wia_upcase_init(void);
typedef int (WINAPI *FPCP)(const wchar_t*, const wchar_t*, wchar_t*);
static FPCP sys;

#define ROT 8

typedef struct { const wchar_t* a; const wchar_t* b; wchar_t* o[ROT]; unsigned idx; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)wia_pathcommonprefixw(k->a, k->b, k->o[i]) + k->o[i][0];
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)sys(k->a, k->b, k->o[i]) + k->o[i][0];
}
#pragma optimize("", on)

static wchar_t* spool;
static unsigned char* dpool;
static unsigned long scur, dcur;
#define SLOT 4096

/* n characters, a separator every `every` (0 = none); `flip` case-flips every letter;
   `diff` puts a mismatching character at that index (-1 = none). */
static const wchar_t* mk(int n, int every, int flip, int diff)
{
    wchar_t* p = spool + scur;
    int k;
    for (k = 0; k < n; ++k) {
        wchar_t c = (every && (k % every) == (every - 1)) ? L'\\' : (wchar_t)(L'a' + k % 26);
        if (flip && c != L'\\') c = (wchar_t)(c - 32);
        p[k] = c;
    }
    if (diff >= 0 && diff < n) p[diff] = L'#';
    p[n] = 0;
    scur += SLOT / 2;                       /* SLOT bytes = SLOT/2 wide characters */
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FPCP)GetProcAddress(h, "PathCommonPrefixW");
    if (!sys) { printf("cannot resolve PathCommonPrefixW\n"); return 1; }
    wia_upcase_init();
    spool = (wchar_t*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    dpool = (unsigned char*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!spool || !dpool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 12 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[N] = {
        "8 identical", "16 identical", "32 identical", "64 identical", "128 identical",
        "254 identical  <== 698 ns", "254, 1 component", "254, differ at 128", "254, differ at 8",
        "differ at 0", "254 case-differing", "UNC 254 identical" };

    {
        int i = 0;
        C[0].a = mk(8, 4, 0, -1);      C[0].b = mk(8, 4, 0, -1);
        C[1].a = mk(16, 4, 0, -1);     C[1].b = mk(16, 4, 0, -1);
        C[2].a = mk(32, 4, 0, -1);     C[2].b = mk(32, 4, 0, -1);
        C[3].a = mk(64, 8, 0, -1);     C[3].b = mk(64, 8, 0, -1);
        C[4].a = mk(128, 8, 0, -1);    C[4].b = mk(128, 8, 0, -1);
        C[5].a = mk(254, 8, 0, -1);    C[5].b = mk(254, 8, 0, -1);
        C[6].a = mk(254, 0, 0, -1);    C[6].b = mk(254, 0, 0, -1);
        C[7].a = mk(254, 8, 0, -1);    C[7].b = mk(254, 8, 0, 128);
        C[8].a = mk(254, 8, 0, -1);    C[8].b = mk(254, 8, 0, 8);
        C[9].a = mk(254, 8, 0, -1);    C[9].b = mk(254, 8, 0, 0);
        C[10].a = mk(254, 8, 0, -1);   C[10].b = mk(254, 8, 1, -1);
        {   /* UNC: "\\" then the same body */
            wchar_t* u1 = spool + scur; scur += SLOT / 2;
            wchar_t* u2 = spool + scur; scur += SLOT / 2;
            const wchar_t* body = C[5].a;
            u1[0] = u1[1] = L'\\'; u2[0] = u2[1] = L'\\';
            for (int k = 0; body[k]; ++k) { u1[k+2] = body[k]; u2[k+2] = body[k]; }
            u1[2 + 254] = 0; u2[2 + 254] = 0;
            C[11].a = u1; C[11].b = u2;
        }
        for (i = 0; i < N; ++i) {
            C[i].idx = 0;
            for (int q = 0; q < ROT; ++q) {
                /* staggered within the page, not all at offset 0 -- see change 250 */
                C[i].o[q] = (wchar_t*)(dpool + dcur + (unsigned long)q * 256);
                dcur += SLOT;
            }
            if (dcur > 3800000 || scur > 3800000) {
                printf("BENCH SETUP ERROR: pool too small\n"); return 1;
            }
            cs[i].label = names[i];
            cs[i].bytes = wcslen(C[i].a) * sizeof(wchar_t);
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }

    /* PER-ROW DIAGNOSTIC: what each row actually asked for and what came back. A row whose shape is
       not what its label says is the most expensive mistake this project makes -- and here it would
       be silent, because a wrong subject still returns a plausible number. */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each:\n");
        for (int i = 0; i < N; ++i) {
            wchar_t o[600];
            int r = wia_pathcommonprefixw(C[i].a, C[i].b, o);
            double t;
            printf("  %-28s len=%3d -> %3d  \"%.10ls%s\"", names[i],
                   (int)wcslen(C[i].a), r, o, wcslen(o) > 10 ? "..." : "");
            t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
            printf("   ours(60) %8.2f ns\n", t);
        }
        printf("\n");
    }

    /* warm every row's whole rotation, both sides, so no row pays a first-touch cost the others
       do not -- change 250's table had its first row reading high for exactly that reason */
    {
        volatile uint64_t sink = 0;
        for (int w = 0; w < 200; ++w)
            for (int i = 0; i < N; ++i) { sink += cs[i].ours(cs[i].ctx);
                                          sink += cs[i].system(cs[i].ctx); }
    }

    return wia_bench_compare("shlwapi PathCommonPrefixW (wia lockstep AVX2 walk vs a per-component "
                             "scalar compare; GB/s counts pszFile1 bytes)", cs, N, 300);
}
