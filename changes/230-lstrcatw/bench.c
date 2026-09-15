// changes/230-lstrcatw/bench.c
// Gate 2: time wia_lstrcatw against the live kernelbase!lstrcatW.
// Pool offsets are COMPUTED with guaranteed spacing -- hand-placed ones let case buffers overlap
// twice while change 228 was being written, and the only thing that caught it was a row reporting
// a throughput above memcpy's.
//
// THE CASE MIX. lstrcat is a destination scan plus a source copy, and those scale with different
// inputs. The short-onto-short rows are here because the NARROW sibling (change 228) had to be
// PARKED for losing there, and the same question has to be asked of this one honestly rather than
// avoided. "64 onto 4000" is the shape the function is usually used in and the survey's worst
// number: 1643 ns.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern wchar_t* wia_lstrcatw(wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

typedef struct { wchar_t* d; int dn; const wchar_t* s; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; k->d[k->dn]=0;
                                  return (uint64_t)(size_t)wia_lstrcatw(k->d, k->s); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; k->d[k->dn]=0;
                                  return (uint64_t)(size_t)sys(k->d, k->s); }
#pragma optimize("", on)

static wchar_t dpool[24576];
static wchar_t spool[16384];

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcatW");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcatW") : 0; }

    enum { N = 9 };
    static const int DN[N] = {    0,    8,   16,    0,    0,   64,  1024,  4000,  4000 };
    static const int SN[N] = {    8,    8,   16,   64, 4000,   64,    64,    64,  4000 };
    static const char* names[] = {"8 onto empty","8 onto 8","16 onto 16",
                                  "64 onto empty","4000 onto empty",
                                  "64 onto 64","64 onto 1024","64 onto 4000","4000 onto 4000"};
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int dcur = 0, scur = 0;
        for (int i = 0; i < N; ++i) {
            wchar_t* dp = dpool + dcur;
            for (int k = 0; k < DN[i]; ++k) dp[k] = (wchar_t)(L'a' + k % 23);
            dp[DN[i]] = 0;
            wchar_t* sp = spool + scur;
            for (int k = 0; k < SN[i]; ++k) sp[k] = (wchar_t)(L'A' + k % 26);
            sp[SN[i]] = 0;
            C[i].d = dp; C[i].dn = DN[i]; C[i].s = sp;
            dcur += DN[i] + SN[i] + 64;
            scur += SN[i] + 64;
            if (dcur > 24000 || scur > 16000) { printf("BENCH SETUP ERROR\n"); return 1; }
            bytes[i] = (size_t)(DN[i] + SN[i]) * sizeof(wchar_t);
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("kernelbase lstrcatW (wia AVX2 clamped scan + append vs kernelbase SSE2)", cs, N, 300);
}
