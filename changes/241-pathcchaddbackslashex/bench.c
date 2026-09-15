/* changes/241-pathcchaddbackslashex/bench.c
   Gate 2: time both Ex forms against the live kernelbase exports.

   THE RESTORE UNDOES EXACTLY WHAT THE CALL DID, AND NOTHING MORE. That is not a convenience, it is
   what makes these rows measure the functions at all.

   Both of these write AT MOST ONE OR TWO CHARACTERS: AddBackslashEx appends a separator and a
   terminator at p[n] and p[n+1]; RemoveBackslashEx writes one terminator over the trailing separator
   at p[e]. Undoing that needs a single 2-byte store -- put back the terminator the append overwrote,
   or put back the separator the removal overwrote.

   The first version of this file restored with a full memcpy of the whole string, and on the
   16-character append row that memcpy cost about 7.6 ns against a function that costs 2.7 -- roughly
   three quarters of the measured time was spent undoing a four-byte change, and the row came out at
   0.84x. That is the same mistake change 238's benchmark made and had to be rebuilt for: a restore
   heavier than the function does not add noise, IT REPLACES THE MEASUREMENT. Both sides paid it
   equally, so the comparison was fair -- and it was fair about the wrong quantity.

   The setup ASSERTS, for every row, that the minimal restore really does return the buffer to
   byte-identical -- so the row cannot silently drift into measuring a function applied to a string it
   has already modified.

   THE CASE MIX. discovery/kernelbase_pathcch.c measured both at 0.101 ns per byte on a 1000-character
   path: 202 ns for work that is "find the end, then write at most one character". The whole cost is
   the length scan, so the rows vary length and nothing else -- there is no early exit to exploit and
   no short-component effect as in change 240, because both functions must reach the terminator before
   they can do anything. The declining paths, which write nothing at all and therefore need no restore,
   are timed in a second table. Lengths are COMPUTED, never hardcoded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

#define PATHCCH_MAX_CCH 0x8000

extern HRESULT wia_pathcchaddbackslashex(wchar_t*, size_t, wchar_t**, size_t*);
extern HRESULT wia_pathcchremovebackslashex(wchar_t*, size_t, wchar_t**, size_t*);
typedef HRESULT (WINAPI *FN)(PWSTR, size_t, PWSTR*, size_t*);
static FN sys_add, sys_rem;

/* fixup: the single slot the call overwrites, and the character that belongs there */
typedef struct { wchar_t* buf; size_t n; size_t fix_at; wchar_t fix_ch; int which; } CASE;

#pragma optimize("", off)
/* The minimal restore: one store, undoing exactly the one the call made. */
static uint64_t op_ours_r(void* c){
    CASE* k = (CASE*)c;
    wchar_t* e; size_t r;
    HRESULT h = k->which ? wia_pathcchremovebackslashex(k->buf, PATHCCH_MAX_CCH, &e, &r)
                         : wia_pathcchaddbackslashex(k->buf, PATHCCH_MAX_CCH, &e, &r);
    k->buf[k->fix_at] = k->fix_ch;
    return (uint64_t)h;
}
static uint64_t op_sys_r(void* c){
    CASE* k = (CASE*)c;
    PWSTR e; size_t r;
    HRESULT h = k->which ? sys_rem(k->buf, PATHCCH_MAX_CCH, &e, &r)
                         : sys_add(k->buf, PATHCCH_MAX_CCH, &e, &r);
    k->buf[k->fix_at] = k->fix_ch;
    return (uint64_t)h;
}
/* the declining paths write nothing, so there is nothing to undo */
static uint64_t op_ours_n(void* c){
    CASE* k = (CASE*)c;
    wchar_t* e; size_t r;
    return (uint64_t)(k->which ? wia_pathcchremovebackslashex(k->buf, PATHCCH_MAX_CCH, &e, &r)
                               : wia_pathcchaddbackslashex(k->buf, PATHCCH_MAX_CCH, &e, &r));
}
static uint64_t op_sys_n(void* c){
    CASE* k = (CASE*)c;
    PWSTR e; size_t r;
    return (uint64_t)(k->which ? sys_rem(k->buf, PATHCCH_MAX_CCH, &e, &r)
                               : sys_add(k->buf, PATHCCH_MAX_CCH, &e, &r));
}
#pragma optimize("", on)

static wchar_t pool[80000];

/* an absolute path of n characters; trail=1 makes the last one a separator */
static void mk(wchar_t* p, int n, int trail)
{
    int k = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n) {
        for (int i = 0; i < 7 && k < n; ++i) p[k++] = (wchar_t)(L'a' + i);
        if (k < n) p[k++] = L'\\';
    }
    if (trail && n > 3) p[n-1] = L'\\';
    p[n] = 0;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys_add = (FN)GetProcAddress(h, "PathCchAddBackslashEx");
    sys_rem = (FN)GetProcAddress(h, "PathCchRemoveBackslashEx");

    enum { N = 10 };
    static const int  LEN  [N] = {  16,   64,  260, 1000, 4000,   16,   64,  260, 1000, 4000 };
    static const int  WHICH[N] = {   0,    0,    0,    0,    0,    1,    1,    1,    1,    1 };
    static const char* names[N] = {
        "add 16", "add 64", "add 260", "add 1000", "add 4000",
        "rem 16", "rem 64", "rem 260", "rem 1000", "rem 4000",
    };
    static CASE C[N]; static wia_case cs[N];
    {
        int cur = 0;
        for (int i = 0; i < N; ++i) {
            int n = LEN[i];
            wchar_t* b = pool + cur;
            cur += n + 40;
            if (cur > 78000) { printf("BENCH SETUP ERROR: pool overflow\n"); return 1; }
            /* add rows must NOT end in a separator; rem rows must */
            mk(b, n, WHICH[i]);
            C[i].buf = b; C[i].n = (size_t)n; C[i].which = WHICH[i];
            if (WHICH[i] == 0) { C[i].fix_at = (size_t)n;     C[i].fix_ch = 0; }
            else               { C[i].fix_at = (size_t)n - 1; C[i].fix_ch = L'\\'; }

            /* ASSERT the minimal restore is exact: snapshot, call, restore, compare. Both sides. */
            static wchar_t snap[8192];
            memcpy(snap, b, (size_t)(n + 2) * 2);
            wchar_t* e; size_t r; PWSTR e2; size_t r2;
            HRESULT h1 = WHICH[i] ? wia_pathcchremovebackslashex(b, PATHCCH_MAX_CCH, &e, &r)
                                  : wia_pathcchaddbackslashex(b, PATHCCH_MAX_CCH, &e, &r);
            b[C[i].fix_at] = C[i].fix_ch;
            int bad1 = memcmp(snap, b, (size_t)(n + 1) * 2) != 0;
            HRESULT h2 = WHICH[i] ? sys_rem(b, PATHCCH_MAX_CCH, &e2, &r2)
                                  : sys_add(b, PATHCCH_MAX_CCH, &e2, &r2);
            b[C[i].fix_at] = C[i].fix_ch;
            int bad2 = memcmp(snap, b, (size_t)(n + 1) * 2) != 0;
            if (h1 != S_OK || h2 != S_OK || bad1 || bad2) {
                printf("BENCH SETUP ERROR: row \"%s\" -- ours %08lX live %08lX, restore %s/%s\n",
                       names[i], (unsigned long)h1, (unsigned long)h2,
                       bad1 ? "INEXACT" : "exact", bad2 ? "INEXACT" : "exact");
                return 1;
            }
            cs[i].label = names[i]; cs[i].bytes = (size_t)n * 2;
            cs[i].ours = op_ours_r; cs[i].system = op_sys_r; cs[i].ctx = &C[i];
        }
    }
    int rc = wia_bench_compare("kernelbase PathCchAddBackslashEx + PathCchRemoveBackslashEx "
                               "(wia AVX2 wcslen + O(1) tail vs kernelbase; the restore is ONE store, "
                               "undoing exactly the one store the call made)", cs, N, 300);

    /* ---- the declining paths, which write nothing and so need no restore --------------------- */
    {
        enum { M = 4 };
        static CASE D[M]; static wia_case ds[M];
        static const char* dn[M] = {
            "add 16 declines", "add 4000 declines", "rem 16 declines", "rem 4000 declines" };
        static const int DL[M] = { 16, 4000, 16, 4000 };
        static const int DW[M] = {  0,    0,   1,    1 };
        int cur = 60000;
        for (int i = 0; i < M; ++i) {
            int n = DL[i];
            wchar_t* b = pool + cur;
            cur += n + 40;
            /* add declines when the path ALREADY ends in a separator; rem declines when it does not */
            mk(b, n, DW[i] ? 0 : 1);
            D[i].buf = b; D[i].n = (size_t)n; D[i].which = DW[i];
            D[i].fix_at = 0; D[i].fix_ch = b[0];
            static wchar_t snap[8192];
            memcpy(snap, b, (size_t)(n + 2) * 2);
            wchar_t* e; size_t r; PWSTR e2; size_t r2;
            HRESULT h1 = DW[i] ? wia_pathcchremovebackslashex(b, PATHCCH_MAX_CCH, &e, &r)
                               : wia_pathcchaddbackslashex(b, PATHCCH_MAX_CCH, &e, &r);
            HRESULT h2 = DW[i] ? sys_rem(b, PATHCCH_MAX_CCH, &e2, &r2)
                               : sys_add(b, PATHCCH_MAX_CCH, &e2, &r2);
            if (h1 != S_FALSE || h2 != S_FALSE || memcmp(snap, b, (size_t)(n + 2) * 2) != 0) {
                printf("BENCH SETUP ERROR: row \"%s\" is not a clean no-write "
                       "(ours %08lX live %08lX buffer %s)\n", dn[i],
                       (unsigned long)h1, (unsigned long)h2,
                       memcmp(snap, b, (size_t)(n + 2) * 2) ? "MODIFIED" : "intact");
                return 1;
            }
            ds[i].label = dn[i]; ds[i].bytes = (size_t)n * 2;
            ds[i].ours = op_ours_n; ds[i].system = op_sys_n; ds[i].ctx = &D[i];
        }
        int rc2 = wia_bench_compare("the DECLINING paths, which write nothing and so need no restore "
                                    "at all (asserted above)", ds, M, 300);
        if (rc2) rc = rc2;
    }
    return rc;
}
