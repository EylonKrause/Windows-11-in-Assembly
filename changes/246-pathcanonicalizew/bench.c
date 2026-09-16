// changes/246-pathcanonicalizew/bench.c
// Gate 2: time wia_pathcanonicalizew against the live shlwapi!PathCanonicalizeW.
//
// THIS CHANGE IS AN ENVELOPE, so its speed is change 243's speed minus a handful of instructions,
// and the case mix is 243's for that reason: the shapes whose cost lives in the dot/dot-dot walk
// rather than in the copy. What is genuinely new here is the pair of rows at the MAX_PATH boundary,
// because this envelope hard-codes cch = MAX_PATH and the shipped function's result cap is 259
// characters -- so an input whose canonical form is 259 characters succeeds and 260 fails, and the
// failure path is a different amount of work in both implementations.
//
// NO RESTORE IS NEEDED: the destination is a separate buffer that is never read back, and the source
// is never modified. The destination still ROTATES across four PAGE-ALIGNED slots, for the reason
// change 245 had to learn the hard way -- its first benchmark cut its buffers out of .bss at
// whatever offsets the setup loop produced and was not reproducible, reading 2371, 2378 and then 289
// ns for the same call. Page-aligned slots make each row's source-to-destination relationship fixed
// from run to run.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int  wia_pathcanonicalizew(wchar_t*, const wchar_t*);
extern void wia_pccx_set_fallback(void*);
typedef BOOL (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

#define ROT 4
#define PAGEW 2048
typedef struct { const wchar_t* s; wchar_t* d[ROT]; unsigned idx; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)wia_pathcanonicalizew(k->d[i], k->s);
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    unsigned i = k->idx; k->idx = (i + 1) & (ROT - 1);
    return (uint64_t)(uint32_t)sys(k->d[i], k->s);
}
#pragma optimize("", on)

static wchar_t* spool;
static wchar_t* dpool;
static unsigned long scur, dcur;

static const wchar_t* mk_plain(int n)
{
    wchar_t* p = spool + scur;
    int k = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n) p[k++] = (k % 9 == 8) ? L'\\' : (wchar_t)(L'a' + k % 23);
    p[n] = 0;
    scur += PAGEW;
    return p;
}
/* a path of n characters that canonicalises DOWN, so the walk does the work */
static const wchar_t* mk_dotdot(int n)
{
    wchar_t* p = spool + scur;
    int k = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n - 6) { p[k++] = L'a'; p[k++] = L'\\'; p[k++] = L'.'; p[k++] = L'.'; p[k++] = L'\\'; }
    while (k < n) p[k++] = L'b';
    p[n] = 0;
    scur += PAGEW;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathCanonicalizeW");
    if (!sys) { printf("cannot resolve PathCanonicalizeW\n"); return 1; }
    wia_pccx_set_fallback((void*)GetProcAddress(LoadLibraryW(L"kernelbase.dll"),
                                                "PathCchCanonicalizeEx"));
    spool = (wchar_t*)VirtualAlloc(0, 1 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    dpool = (wchar_t*)VirtualAlloc(0, 1 << 20, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!spool || !dpool) { printf("BENCH SETUP ERROR: VirtualAlloc\n"); return 1; }

    enum { N = 9 };
    static CASE C[N]; static wia_case cs[N];
    static const wchar_t* subj[N];
    static const char* names[N] = {
        "C:\\dir\\file.txt", "64 plain", "128 plain", "259 plain (the cap)",
        "260 plain (refused)", "64 with ..", "128 with ..", "259 with ..", "600 with .. -> 5 chars" };
    subj[0] = L"C:\\dir\\file.txt";
    subj[1] = mk_plain(64);
    subj[2] = mk_plain(128);
    subj[3] = mk_plain(259);
    subj[4] = mk_plain(260);
    subj[5] = mk_dotdot(64);
    subj[6] = mk_dotdot(128);
    subj[7] = mk_dotdot(259);
    subj[8] = mk_dotdot(600);

    for (int i = 0; i < N; ++i) {
        C[i].s = subj[i]; C[i].idx = 0;
        for (int q = 0; q < ROT; ++q) { C[i].d[q] = dpool + dcur; dcur += PAGEW; }
        cs[i].label = names[i];
        cs[i].bytes = wcslen(subj[i]) * sizeof(wchar_t);
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }

    /* per-row shape, so a row whose label does not match what it asked for cannot hide */
    {
        volatile uint64_t sink = 0;
        printf("per-row shape, one call each:\n");
        for (int i = 0; i < N; ++i) {
            int r = wia_pathcanonicalizew(C[i].d[0], C[i].s);
            double t = wia_measure(cs[i].ours, cs[i].ctx, 60, &sink);
            printf("  %-24s inLen=%4llu -> BOOL=%d result len=%3d   ours(60) %8.2f ns\n",
                   names[i], (unsigned long long)wcslen(C[i].s), r, (int)wcslen(C[i].d[0]), t);
        }
        printf("\n");
    }

    return wia_bench_compare("shlwapi PathCanonicalizeW (wia envelope over change 243's core vs the "
                             "shipped envelope over the shipped body)", cs, N, 300);
}
