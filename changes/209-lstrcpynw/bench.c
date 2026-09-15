// changes/209-lstrcpynw/bench.c
// Gate 2: time wia_lstrcpynw against the live kernelbase!lstrcpynW.
//
// The classes span the two regimes that matter: short bounded copies, which is what almost every
// real caller does (MAX_PATH-shaped buffers), and long ones where the 16-character chunked path
// actually runs. The truncating class is included because a bound shorter than the source is the
// whole point of this API, and n==0 because it is a documented no-op that still returns.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern wchar_t* wia_lstrcpynw(wchar_t*, const wchar_t*, int);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*, int);
static FN sys;

typedef struct { const wchar_t* s; int n; } CASE;
static wchar_t obuf[8192];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)(size_t)wia_lstrcpynw(obuf,k->s,k->n); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)(size_t)sys(obuf,k->s,k->n); }
#pragma optimize("", on)

static wchar_t S8[16], S16[32], S64[128], S260[512], S4000[8192];

static void fill(wchar_t* p, int n){
    for (int i = 0; i < n; ++i) p[i] = (wchar_t)(L'a' + (i % 26));
    p[n] = 0;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "lstrcpynW");
    if (!sys) { h = LoadLibraryW(L"kernel32.dll"); sys = (FN)GetProcAddress(h, "lstrcpynW"); }

    fill(S8, 8); fill(S16, 16); fill(S64, 64); fill(S260, 260); fill(S4000, 4000);

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "8 chars", "16 chars", "64 chars", "260 (MAX_PATH)", "4000 chars",
        "4000 src, n=64 (truncates)", "n=0 (no-op)" };
    C[0].s=S8;    C[0].n=16;
    C[1].s=S16;   C[1].n=32;
    C[2].s=S64;   C[2].n=128;
    C[3].s=S260;  C[3].n=300;
    C[4].s=S4000; C[4].n=4096;
    C[5].s=S4000; C[5].n=64;
    C[6].s=S260;  C[6].n=0;

    static const size_t bytes[] = { 16, 32, 128, 520, 8000, 126, 1 };
    for (int i = 0; i < N; ++i) {
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("kernelbase lstrcpynW (wia page-safe AVX2 copy vs a per-character loop)",
                             cs, N, 300);
}
