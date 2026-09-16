// changes/177-pathisprefixw/bench.c
// Gate 2: time wia_pathisprefixw against the live shlwapi!PathIsPrefixW.
//
// THE ROW THIS CHANGE EXISTS FOR is "254 true": discovery measured the shipped export at 606 ns on
// it, and the identity explains why -- it is paying for PathCommonPrefixW's per-component walk and
// then comparing a length.
//
// THE CASE MIX. Both implementations walk only as far as the two paths agree, so the rows vary that
// and nothing else:
//
//   * TRUE rows at 8, 32, 128 and 254 characters -- the prefix IS the path, so the walk runs to the
//     end and the comparison succeeds. These are the rows that pay for everything.
//   * FALSE rows where the divergence is early (8) or late (128 of 254): the same function, stopped
//     at different points, which separates the walk from the fixed cost.
//   * "254, trailing sep" -- the prefix is the path plus a '\'. The walk runs the whole way and the
//     answer is still FALSE, because the common prefix is 254 and wcslen(prefix) is 255. That is
//     the case the original probing found surprising, and it is the worst combination of "does all
//     the work" and "returns FALSE".
//   * "254 case-differing" -- a genuine prefix that differs from the path only in case, so change
//     167's block-fold path runs on every block. That row was this project's own worst case in 167
//     until the fold filter landed, and it is carried forward here rather than dropped.
//
// Nothing to restore: both inputs are read-only and there is no output buffer at all -- this
// function returns a BOOL. The rotation that other benchmarks need for their output slots is
// therefore absent by construction, and with it the 4K-aliasing trap that made change 250's table
// report a regression that was not there.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int  wia_pathisprefixw(const wchar_t*, const wchar_t*);
extern void wia_upcase_init(void);
typedef BOOL (WINAPI *FPIP)(const wchar_t*, const wchar_t*);
static FPIP sys;

typedef struct { const wchar_t* pre; const wchar_t* path; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)(uint32_t)wia_pathisprefixw(k->pre, k->path);
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)(uint32_t)(sys(k->pre, k->path) != 0);
}
#pragma optimize("", on)

/* page-aligned subjects, one per slot -- a buffer's ADDRESS has decided this project's verdicts
   before (changes 142, 228, 230, 241, 245 and 250), so none of them share a cache line here */
static wchar_t* pool;
static unsigned long cur;
#define SLOT 2048                       /* wide characters */

static wchar_t* mk(int n, int every, int flip, int diff)
{
    wchar_t* p = pool + cur;
    int k;
    for (k = 0; k < n; ++k) {
        wchar_t c = (every && (k % every) == (every - 1)) ? L'\\' : (wchar_t)(L'a' + k % 26);
        if (flip && c != L'\\') c = (wchar_t)(c - 32);
        p[k] = c;
    }
    if (diff >= 0 && diff < n) p[diff] = L'#';
    p[n] = 0;
    cur += SLOT;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FPIP)GetProcAddress(h, "PathIsPrefixW");
    if (!sys) { printf("cannot resolve PathIsPrefixW\n"); return 1; }
    wia_upcase_init();
    pool = (wchar_t*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!pool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 9 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[N] = {
        "8 true", "32 true", "128 true", "254 true  <== 606 ns", "254, differ at 128",
        "254, differ at 8", "254, trailing sep (FALSE)", "254 case-differing (TRUE)",
        "empty prefix (TRUE)" };

    {
        wchar_t* p254 = mk(254, 8, 0, -1);
        C[0].path = mk(8, 4, 0, -1);    C[0].pre = mk(8, 4, 0, -1);
        C[1].path = mk(32, 8, 0, -1);   C[1].pre = mk(32, 8, 0, -1);
        C[2].path = mk(128, 8, 0, -1);  C[2].pre = mk(128, 8, 0, -1);
        C[3].path = p254;               C[3].pre = mk(254, 8, 0, -1);
        C[4].path = p254;               C[4].pre = mk(254, 8, 0, 128);
        C[5].path = p254;               C[5].pre = mk(254, 8, 0, 8);
        {   /* the path plus a trailing separator: all the work, and still FALSE */
            wchar_t* q = mk(254, 8, 0, -1);
            q[254] = L'\\'; q[255] = 0;
            C[6].path = p254; C[6].pre = q;
        }
        C[7].path = p254;               C[7].pre = mk(254, 8, 1, -1);
        C[8].path = p254;               C[8].pre = mk(0, 0, 0, -1);
        for (int i = 0; i < N; ++i) {
            cs[i].label = names[i];
            cs[i].bytes = wcslen(C[i].path) * sizeof(wchar_t);
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
        if (cur > 1800000) { printf("BENCH SETUP ERROR: pool too small\n"); return 1; }
    }

    /* PER-ROW DIAGNOSTIC: what each row asked for and what came back. The observable here is a
       single BOOL, so a row whose shape is not what its label says would be completely silent --
       "254 true" quietly returning FALSE would still produce a plausible number. */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each:\n");
        for (int i = 0; i < N; ++i) {
            int r = wia_pathisprefixw(C[i].pre, C[i].path);
            int l = sys(C[i].pre, C[i].path) != 0;
            double t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
            printf("  %-28s preLen=%3d pathLen=%3d -> ours=%d live=%d%s   ours(60) %8.2f ns\n",
                   names[i], (int)wcslen(C[i].pre), (int)wcslen(C[i].path), r, l,
                   r == l ? "" : "  <== DISAGREE", t);
        }
        printf("\n");
    }

    return wia_bench_compare("shlwapi PathIsPrefixW (wia = change 167's walk behind a NULL check "
                             "and a length comparison; GB/s counts pszPath bytes)", cs, N, 300);
}
