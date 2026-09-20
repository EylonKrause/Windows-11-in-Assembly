// changes/251-pathissamerootw/bench.c
// Gate 2: time wia_pathissamerootw against the live shlwapi!PathIsSameRootW.
//
// The row this change exists for is "254, same root": discovery measured the shipped export at
// 603.60 ns on a 254-character path -- 5.73 ns per character, the THIRD-HIGHEST per-byte cost of
// every shlwapi export this project had not converted.
//
// And that cost is not the root skip. The root skip is bounded work on a handful of characters --
// the shipped PathCchSkipRoot measures 5.16 ns on a 250-character path against 5.21 ns on a short
// one, i.e. FLAT. All 603 ns of it is PathCommonPrefixW walking the two paths component by
// component, which is exactly what change 167 replaced. So the rows vary how far the two paths
// AGREE, because that is the only thing either implementation actually walks:
//
//   * "same root" rows at 8, 32, 128 and 254 characters -- identical paths, so the walk runs to the
//     end. These pay for everything.
//   * "different root" -- the walk stops at once and the answer is FALSE. If a vector prologue cost
//     anything, this is the row that would show it.
//   * "same root, diverge at 8" -- the roots match, the paths do not, and the answer is still TRUE.
//     That is the case the function is actually FOR, and it is the one where the shipped code does
//     the least work, so it is the hardest row to beat.
//   * "UNC" and "extended prefix" -- the two root forms that are not a drive letter, so the root
//     parser's own branches are represented rather than assumed.
//   * "no root" -- a relative path, where PathSkipRootW returns NULL and neither implementation
//     walks anything at all.
//
// Nothing to restore: both inputs are read-only and there is no output buffer -- the observable is a
// BOOL. There is therefore no rotation and no 4K-aliasing trap of the kind that made change 250's
// table report a regression that was not there.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int  wia_pathissamerootw(const wchar_t*, const wchar_t*);
extern void wia_upcase_init(void);
typedef BOOL (WINAPI *FSAME)(const wchar_t*, const wchar_t*);
static FSAME sys;

typedef struct { const wchar_t* a; const wchar_t* b; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)(uint32_t)wia_pathissamerootw(k->a, k->b);
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)(uint32_t)(sys(k->a, k->b) != 0);
}
#pragma optimize("", on)

/* page-aligned subjects: a buffer's ADDRESS has decided this project's verdicts before */
static wchar_t* pool;
static unsigned long cur;
#define SLOT 1024                       /* wide characters */

static wchar_t* mk(const wchar_t* root, int n, int diff)
{
    wchar_t* p = pool + cur;
    int r = (int)wcslen(root), k;
    wcscpy(p, root);
    for (k = 0; k < n; ++k)
        p[r + k] = (k % 7 == 6) ? L'\\' : (wchar_t)(L'a' + k % 26);
    if (diff >= 0 && diff < n) p[r + diff] = L'#';
    p[r + n] = 0;
    cur += SLOT;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FSAME)GetProcAddress(h, "PathIsSameRootW");
    if (!sys) { printf("cannot resolve PathIsSameRootW\n"); return 1; }
    wia_upcase_init();
    pool = (wchar_t*)VirtualAlloc(0, 4 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!pool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 10 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[N] = {
        "8, same root", "32, same root", "128, same root", "254, same root  <== 604 ns",
        "254, same root, diverge at 8", "254, different root", "254 UNC, same root",
        "254 \\\\?\\ prefix, same root", "254, no root (relative)", "254, case-differing root" };

    {
        C[0].a = mk(L"C:\\", 8, -1);     C[0].b = mk(L"C:\\", 8, -1);
        C[1].a = mk(L"C:\\", 32, -1);    C[1].b = mk(L"C:\\", 32, -1);
        C[2].a = mk(L"C:\\", 128, -1);   C[2].b = mk(L"C:\\", 128, -1);
        C[3].a = mk(L"C:\\", 254, -1);   C[3].b = mk(L"C:\\", 254, -1);
        C[4].a = mk(L"C:\\", 254, -1);   C[4].b = mk(L"C:\\", 254, 8);
        C[5].a = mk(L"C:\\", 254, -1);   C[5].b = mk(L"D:\\", 254, -1);
        C[6].a = mk(L"\\\\srv\\share\\", 254, -1);
        C[6].b = mk(L"\\\\srv\\share\\", 254, -1);
        C[7].a = mk(L"\\\\?\\C:\\", 254, -1);
        C[7].b = mk(L"\\\\?\\C:\\", 254, -1);
        C[8].a = mk(L"rel\\", 254, -1);  C[8].b = mk(L"rel\\", 254, -1);
        C[9].a = mk(L"C:\\", 254, -1);   C[9].b = mk(L"c:\\", 254, -1);
        for (int i = 0; i < N; ++i) {
            cs[i].label = names[i];
            cs[i].bytes = wcslen(C[i].a) * sizeof(wchar_t);
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
        if (cur > 1800000) { printf("BENCH SETUP ERROR: pool too small\n"); return 1; }
    }

    /* PER-ROW DIAGNOSTIC. The observable is one BOOL, so a row whose shape is not what its label
       says would be completely silent -- "same root" quietly answering FALSE would still produce a
       perfectly plausible number. Both answers are printed, and they have to agree. */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each:\n");
        for (int i = 0; i < N; ++i) {
            int r = wia_pathissamerootw(C[i].a, C[i].b);
            int l = sys(C[i].a, C[i].b) != 0;
            double t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
            printf("  %-32s len=%3d -> ours=%d live=%d%s   ours(60) %8.2f ns\n",
                   names[i], (int)wcslen(C[i].a), r, l, r == l ? "" : "  <== DISAGREE", t);
        }
        printf("\n");
    }

    return wia_bench_compare("shlwapi PathIsSameRootW (wia = a scalar root skip + change 167's walk; "
                             "GB/s counts pszPath1 bytes)", cs, N, 300);
}
