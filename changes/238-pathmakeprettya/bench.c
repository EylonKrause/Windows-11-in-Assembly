/* changes/238-pathmakeprettya/bench.c
   Gate 2: time wia_pathmakeprettya against the live shlwapi!PathMakePrettyA.

   THE RESTORE IS PAID ONLY WHERE IT IS NEEDED, and that distinction matters more here than in any
   in-place benchmark in this project so far.

   A rewritten case modifies the buffer, so it must be restored every iteration and both sides pay
   that memcpy -- the usual arrangement. But A REFUSED CASE WRITES NOTHING AT ALL: correctness.c
   compares a 1024-byte poison window on every one of its half-million cases and proves it. So a
   restore on those rows would undo nothing, and including one does not merely add noise -- IT
   REPLACES THE MEASUREMENT. The first version of this file restored unconditionally and reported
   "4000, refused at 1" at 1.04x, because a 4000-byte memcpy costs about 30 ns on both sides while the
   refusal it was supposed to be timing costs about one. That number was an artifact of memcpy racing
   memcpy, and reporting it as this function's ratio would have been misleading.

   So the rewritten rows restore and the refused rows do not, the table says which is which, and the
   setup ASSERTS that each refused case really leaves its buffer byte-identical rather than assuming
   it. Change 232's benchmark, where every row shares a restore, is the contrast worth comparing
   against: its shortest classes are compressed toward 1 for exactly this reason.

   THE CASE MIX. The function has two branches with very different costs and both must appear:

     * REFUSED. One ASCII lowercase letter anywhere vetoes, the scan stops there, nothing is written.
       A path that is already lowercase -- the overwhelmingly common real input -- is refused at its
       first letter. The cost is the DISTANCE to that letter, not the length of the path, which is
       why the vetoing letter is placed both early and late.
     * REWRITTEN. An all-uppercase path is scanned in full and then rewritten in full. That is where
       the shipped 7.48 ns per byte lives.

   Lengths are COMPUTED, never hardcoded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern int wia_pathmakeprettya(char*);
typedef int (WINAPI *FN)(char*);
static FN sys;

typedef struct { const char* src; size_t n; char* work; } CASE;

#pragma optimize("", off)
/* rewritten: the buffer is modified, so both sides pay the restore */
static uint64_t op_ours_r(void* c){
    CASE* k = (CASE*)c;
    memcpy(k->work, k->src, k->n + 1);
    return (uint64_t)wia_pathmakeprettya(k->work);
}
static uint64_t op_sys_r(void* c){
    CASE* k = (CASE*)c;
    memcpy(k->work, k->src, k->n + 1);
    return (uint64_t)sys(k->work);
}
/* refused: the function writes nothing, so there is nothing to restore and no memcpy to hide
   behind. Asserted at setup, not assumed. */
static uint64_t op_ours_n(void* c){ return (uint64_t)wia_pathmakeprettya(((CASE*)c)->work); }
static uint64_t op_sys_n (void* c){ return (uint64_t)sys(((CASE*)c)->work); }
#pragma optimize("", on)

static char pool[65536];
static char work[8192];
static char refbuf[4][8192];

/* veto >= 0 puts an ASCII lowercase letter at that index, which makes the call a refusal */
static const char* mk(int off, int n, int veto)
{
    char* p = pool + off;
    for (int i = 0; i < n; ++i) p[i] = (i % 8 == 7) ? '\\' : (char)('A' + i % 23);
    if (veto >= 0 && veto < n) p[veto] = 'q';
    p[n] = 0;
    return p;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathMakePrettyA");

    enum { N = 8 };
    static const int LEN [N] = {  16,   64,  254,  254, 4000, 4000,   64, 4000 };
    static const int VETO[N] = {  -1,   -1,   -1,    1,   -1,    1,   40,  200 };
    static const char* names[N] = {
        "16, rewritten",
        "64, rewritten",
        "254, rewritten",
        "254, refuse@1",
        "4000, rewritten",
        "4000, refuse@1",
        "64, refuse@40",
        "4000, refuse@200",
    };
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int cur = 0, nref = 0;
        for (int i = 0; i < N; ++i) {
            C[i].src = mk(cur, LEN[i], VETO[i]);
            C[i].n = (size_t)LEN[i];
            cur += LEN[i] + 64;
            if (cur > 60000) { printf("BENCH SETUP ERROR: pool overflow\n"); return 1; }
            /* bytes actually examined: to the vetoing letter, or the whole path */
            bytes[i] = (size_t)(VETO[i] >= 0 ? VETO[i] + 1 : LEN[i]);
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            if (VETO[i] >= 0) {
                C[i].work = refbuf[nref++];
                memcpy(C[i].work, C[i].src, C[i].n + 1);
                /* ASSERT the refusal writes nothing, so dropping the restore is sound */
                int r1 = wia_pathmakeprettya(C[i].work);
                int r2 = sys(C[i].work);
                int dirty = memcmp(C[i].work, C[i].src, C[i].n + 1) != 0;
                if (r1 || r2 || dirty) {
                    printf("BENCH SETUP ERROR: row %d is not a clean refusal "
                           "(ours %d, live %d, buffer %s)\n",
                           i, r1, r2, dirty ? "MODIFIED" : "intact");
                    return 1;
                }
                cs[i].ours = op_ours_n; cs[i].system = op_sys_n;
            } else {
                C[i].work = work;
                cs[i].ours = op_ours_r; cs[i].system = op_sys_r;
            }
            cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("shlwapi PathMakePrettyA (wia AVX2 two-pass map vs shlwapi; "
                             "refused rows pay no restore, because a refusal writes nothing)",
                             cs, N, 300);
}
