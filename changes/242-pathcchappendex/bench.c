/* changes/242-pathcchappendex/bench.c
   Gate 2: time wia_pathcchappendex and wia_pathcchcombineex against the live kernelbase exports.

   THE RESTORE IS MEASURED, NOT GUESSED. PathCchAppendEx works IN PLACE, so its rows must put the base
   back every iteration -- and a restore heavier than the function REPLACES the measurement, which is
   what change 238's first benchmark did and what change 241 had to undo twice. So the setup RUNS each
   row once, finds the first and last character the call actually changed, and restores exactly that
   range: for an ordinary append that is a single 2-byte store putting the terminator back, because the
   base itself is untouched and only the seam and `more` are written. The extent is printed per row so
   the number is auditable rather than asserted.

   PathCchCombineEx writes a SEPARATE output, so its rows need no restore at all.

   THE SIZE CLASSES STOP AT 250 CHARACTERS because the contract stops there: change 243 measured the
   MAX_PATH result cap, so with dwFlags 0 a result of 260 characters or more is
   ERROR_FILENAME_EXCED_RANGE however large cch is. Longer inputs are error rows, and two are included
   as such.

   Lengths are COMPUTED, never hardcoded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

#define MAXCCH 0x8000

extern long wia_pathcchappendex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern long wia_pathcchcombineex(wchar_t*, size_t, const wchar_t*, const wchar_t*, unsigned long);
extern void wia_pcap_set_fallback(void*);
extern void wia_pccb_set_fallback(void*);
typedef HRESULT (WINAPI *FAPP)(PWSTR, size_t, PCWSTR, ULONG);
typedef HRESULT (WINAPI *FCMB)(PWSTR, size_t, PCWSTR, PCWSTR, ULONG);
static FAPP sys_app;
static FCMB sys_cmb;

typedef struct {
    const wchar_t* base;      /* the base, for Append's restore and Combine's input */
    const wchar_t* more;
    wchar_t*       work;      /* Append's in-place buffer, or Combine's output */
    size_t         cch;
    size_t         lo, n;     /* the restore window, in characters: work[lo .. lo+n) */
} CASE;

#pragma optimize("", off)
/* Append: restore exactly the window the call changes, on both sides */
static uint64_t op_ap_ours(void* c){
    CASE* k = (CASE*)c;
    memcpy(k->work + k->lo, k->base + k->lo, k->n * sizeof(wchar_t));
    return (uint64_t)wia_pathcchappendex(k->work, k->cch, k->more, 0);
}
static uint64_t op_ap_sys(void* c){
    CASE* k = (CASE*)c;
    memcpy(k->work + k->lo, k->base + k->lo, k->n * sizeof(wchar_t));
    return (uint64_t)sys_app(k->work, k->cch, k->more, 0);
}
/* Append rows that change nothing: no restore, and nothing for a memcpy to hide behind */
static uint64_t op_ap_ours_n(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)wia_pathcchappendex(k->work, k->cch, k->more, 0);
}
static uint64_t op_ap_sys_n(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)sys_app(k->work, k->cch, k->more, 0);
}
/* Combine: a separate output, so no restore anywhere */
static uint64_t op_cb_ours(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)wia_pathcchcombineex(k->work, k->cch, k->base, k->more, 0);
}
static uint64_t op_cb_sys(void* c){
    CASE* k = (CASE*)c;
    return (uint64_t)sys_cmb(k->work, k->cch, k->base, k->more, 0);
}
#pragma optimize("", on)

static wchar_t pool[60000];
static int pcur;
static wchar_t apwork[8][0x8400];
static wchar_t cbout[0x8400];

/* a drive-absolute path of exactly n characters built from `seg`-character components */
static const wchar_t* mk(int n, int seg)
{
    wchar_t* p = pool + pcur;
    int k = 0;
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n) {
        for (int i = 0; i < seg && k < n; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
        if (k < n) p[k++] = L'\\';
    }
    if (k > 0 && p[k-1] == L'\\') p[k-1] = L'z';
    p[n] = 0;
    pcur += n + 8;
    return p;
}

static const wchar_t* mk_unc(int n, int seg)
{
    wchar_t* p = pool + pcur;
    int k = 0;
    p[k++] = L'\\'; p[k++] = L'\\';
    for (int i = 0; i < 3 && k < n; ++i) p[k++] = L's';
    if (k < n) p[k++] = L'\\';
    for (int i = 0; i < 3 && k < n; ++i) p[k++] = L'h';
    while (k < n) {
        if (k < n) p[k++] = L'\\';
        for (int i = 0; i < seg && k < n; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
    }
    if (k > 0 && p[k-1] == L'\\') p[k-1] = L'z';
    p[n] = 0;
    pcur += n + 8;
    return p;
}

static const wchar_t* mk_pfx(int n, int seg)
{
    wchar_t* p = pool + pcur;
    int k = 0;
    p[k++] = L'\\'; p[k++] = L'\\'; p[k++] = L'?'; p[k++] = L'\\';
    p[k++] = L'C'; p[k++] = L':'; p[k++] = L'\\';
    while (k < n) {
        for (int i = 0; i < seg && k < n; ++i) p[k++] = (wchar_t)(L'a' + (i % 7));
        if (k < n) p[k++] = L'\\';
    }
    if (k > 0 && p[k-1] == L'\\') p[k-1] = L'z';
    p[n] = 0;
    pcur += n + 8;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys_app = (FAPP)GetProcAddress(h, "PathCchAppendEx");
    sys_cmb = (FCMB)GetProcAddress(h, "PathCchCombineEx");
    if (!sys_app || !sys_cmb) { printf("BENCH SETUP ERROR: cannot resolve the exports\n"); return 1; }
    wia_pcap_set_fallback((void*)sys_app);
    wia_pccb_set_fallback((void*)sys_cmb);

    enum { N = 16 };
    static const char* names[N] = {
        "append 16 + x",
        "append 64 + x",
        "append 128 + x",
        "append 250 + x",
        "append 128 + ..\\y",
        "append 128 + (empty)",
        "append unc 128 + x",
        "append \\\\?\\ 128 + x",
        "combine 16 + x",
        "combine 64 + x",
        "combine 128 + x",
        "combine 250 + x",
        "combine 128 + \\y",
        "combine 128 + ..\\..\\y",
        "combine over the cap",
        "combine cch too small",
    };
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    const wchar_t* bs[N]; const wchar_t* ms[N]; size_t cchs[N];
    int is_append[N], nowrite[N];

    bs[0] = mk(16, 5);    ms[0] = L"x";          cchs[0] = MAXCCH; is_append[0]=1; nowrite[0]=0;
    bs[1] = mk(64, 7);    ms[1] = L"x";          cchs[1] = MAXCCH; is_append[1]=1; nowrite[1]=0;
    bs[2] = mk(128, 7);   ms[2] = L"x";          cchs[2] = MAXCCH; is_append[2]=1; nowrite[2]=0;
    bs[3] = mk(250, 7);   ms[3] = L"x";          cchs[3] = MAXCCH; is_append[3]=1; nowrite[3]=0;
    bs[4] = mk(128, 7);   ms[4] = L"..\\y";      cchs[4] = MAXCCH; is_append[4]=1; nowrite[4]=0;
    bs[5] = mk(128, 7);   ms[5] = L"";           cchs[5] = MAXCCH; is_append[5]=1; nowrite[5]=1;
    bs[6] = mk_unc(128,7);ms[6] = L"x";          cchs[6] = MAXCCH; is_append[6]=1; nowrite[6]=0;
    bs[7] = mk_pfx(128,7);ms[7] = L"x";          cchs[7] = MAXCCH; is_append[7]=1; nowrite[7]=0;
    bs[8] = mk(16, 5);    ms[8] = L"x";          cchs[8] = MAXCCH; is_append[8]=0; nowrite[8]=0;
    bs[9] = mk(64, 7);    ms[9] = L"x";          cchs[9] = MAXCCH; is_append[9]=0; nowrite[9]=0;
    bs[10]= mk(128, 7);   ms[10]= L"x";          cchs[10]= MAXCCH; is_append[10]=0; nowrite[10]=0;
    bs[11]= mk(250, 7);   ms[11]= L"x";          cchs[11]= MAXCCH; is_append[11]=0; nowrite[11]=0;
    bs[12]= mk(128, 7);   ms[12]= L"\\y";        cchs[12]= MAXCCH; is_append[12]=0; nowrite[12]=0;
    bs[13]= mk(128, 7);   ms[13]= L"..\\..\\y";  cchs[13]= MAXCCH; is_append[13]=0; nowrite[13]=0;
    bs[14]= mk(300, 7);   ms[14]= L"x";          cchs[14]= MAXCCH; is_append[14]=0; nowrite[14]=0;
    bs[15]= mk(128, 7);   ms[15]= L"x";          cchs[15]= 8;      is_append[15]=0; nowrite[15]=0;

    printf("restore windows (Append only; Combine writes a separate buffer):\n");
    for (int i = 0; i < N; ++i) {
        static wchar_t a[0x8400], b[0x8400];
        long r1, r2;
        size_t blen = wcslen(bs[i]);
        C[i].base = bs[i]; C[i].more = ms[i]; C[i].cch = cchs[i];
        C[i].lo = 0; C[i].n = 0;

        /* every row is verified against the live export before it is timed */
        for (int k = 0; k < 0x600; ++k) { a[k] = 0xCDCD; b[k] = 0xCDCD; }
        if (is_append[i]) {
            memcpy(a, bs[i], (blen+1)*sizeof(wchar_t));
            memcpy(b, bs[i], (blen+1)*sizeof(wchar_t));
            r1 = wia_pathcchappendex(a, cchs[i], ms[i], 0);
            r2 = (long)sys_app(b, cchs[i], ms[i], 0);
        } else {
            r1 = wia_pathcchcombineex(a, cchs[i], bs[i], ms[i], 0);
            r2 = (long)sys_cmb(b, cchs[i], bs[i], ms[i], 0);
        }
        if (r1 != r2 || (r1 == 0 && wcscmp(a, b))) {
            printf("BENCH SETUP ERROR: row %d (%s) disagrees: ours %08lX \"%ls\", live %08lX \"%ls\"\n",
                   i, names[i], (unsigned long)r1, a, (unsigned long)r2, b);
            return 1;
        }

        if (is_append[i]) {
            /* find the window the call actually changed, so the restore is exactly that and no more */
            size_t lo = 0, hi = 0; int found = 0;
            for (size_t k = 0; k <= blen + 8 && k < 0x500; ++k) {
                wchar_t was = (k <= blen) ? bs[i][k] : (wchar_t)0xCDCD;
                if (a[k] != was) { if (!found) { lo = k; found = 1; } hi = k; }
            }
            C[i].work = apwork[i < 8 ? i : 0];
            memcpy(C[i].work, bs[i], (blen+1)*sizeof(wchar_t));
            if (!found) {
                if (!nowrite[i]) {
                    printf("BENCH SETUP ERROR: row %d (%s) was expected to write and did not\n",
                           i, names[i]);
                    return 1;
                }
                printf("  %-24s writes NOTHING: no restore\n", names[i]);
                cs[i].ours = op_ap_ours_n; cs[i].system = op_ap_sys_n;
            } else {
                if (nowrite[i]) {
                    printf("BENCH SETUP ERROR: row %d (%s) was expected to write nothing but changed "
                           "[%zu..%zu]\n", i, names[i], lo, hi);
                    return 1;
                }
                C[i].lo = lo; C[i].n = hi - lo + 1;
                printf("  %-24s restores %zu character(s) at [%zu..%zu] of a %zu-character base\n",
                       names[i], C[i].n, lo, hi, blen);
                cs[i].ours = op_ap_ours; cs[i].system = op_ap_sys;
            }
        } else {
            C[i].work = cbout;
            cs[i].ours = op_cb_ours; cs[i].system = op_cb_sys;
        }
        bytes[i] = (blen + wcslen(ms[i])) * sizeof(wchar_t);
        cs[i].label = names[i]; cs[i].bytes = bytes[i]; cs[i].ctx = &C[i];
    }
    printf("\n");

    return wia_bench_compare("kernelbase PathCchAppendEx + PathCchCombineEx, dwFlags 0 "
                             "(wia join + AVX2 two-segment walk vs kernelbase)", cs, N, 300);
}
