// changes/231-strcatbuffa/bench.c
// Gate 2: time wia_strcatbuffa against the live shlwapi!StrCatBuffA.
// Pool offsets are COMPUTED with guaranteed spacing (see change 228 for why hand-placed ones are a
// trap). The destination is re-terminated every iteration by both sides.
//
// The case mix. StrCatBuff is a bounded scan plus a bounded copy, so the axes are the destination
// length, the source length, and how the bound sits relative to them. The "bound already exceeded"
// row matters because that is the case where the shipped function does the scan and then writes
// nothing at all, pure scan cost, no copy.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern char* wia_strcatbuffa(char*, const char*, int);
typedef char* (WINAPI *FN)(char*, const char*, int);
static FN sys;

typedef struct { char* d; int dn; const char* s; int cch; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; k->d[k->dn]=0;
                                  return (uint64_t)(size_t)wia_strcatbuffa(k->d, k->s, k->cch); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; k->d[k->dn]=0;
                                  return (uint64_t)(size_t)sys(k->d, k->s, k->cch); }
#pragma optimize("", on)

static char dpool[16384];
static char spool[8192];

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"StrCatBuffA");

    enum { N = 8 };
    /* destination length, source length, and the bound as an offset from dn+sn+1 */
    static const int DN[N]    = {   0,   8,  16,  64, 260, 260, 1024, 4000 };
    static const int SN[N]    = {   8,   8,  16,  64,  32,  32,   64,   64 };
    static const int SLACK[N] = {  64,  64,  64,  64,  64, -20,   64,   64 };
    static const char* names[] = {"8 into empty","8 onto 8","16 onto 16","64 onto 64",
                                  "32 onto 260 (the survey's shape)","32 onto 260, TRUNCATING",
                                  "64 onto 1024","64 onto 4000"};
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int dcur = 0, scur = 0;
        for (int i = 0; i < N; ++i) {
            char* dp = dpool + dcur;
            for (int k = 0; k < DN[i]; ++k) dp[k] = (char)('a' + k % 23);
            dp[DN[i]] = 0;
            char* sp = spool + scur;
            for (int k = 0; k < SN[i]; ++k) sp[k] = (char)('A' + k % 26);
            sp[SN[i]] = 0;
            C[i].d = dp; C[i].dn = DN[i]; C[i].s = sp;
            C[i].cch = DN[i] + SN[i] + 1 + SLACK[i];
            dcur += DN[i] + SN[i] + 128;
            scur += SN[i] + 64;
            if (dcur > 16000 || scur > 8000) { printf("BENCH SETUP ERROR\n"); return 1; }
            bytes[i] = (size_t)(DN[i] + SN[i]);
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("shlwapi StrCatBuffA (wia AVX2 bounded scan + clamped append vs shlwapi)", cs, N, 300);
}
