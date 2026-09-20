// changes/229-lstrcpyw/bench.c
// Gate 2: time wia_lstrcpyw against the live kernelbase!lstrcpyW.
// Lengths are COMPUTED, never hardcoded, and the pool offsets are computed with guaranteed spacing
//, hand-placed offsets let one case's buffer land inside another's twice while change 228 was
// being written, and the only reason it was caught was a row reporting a throughput above memcpy's.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern wchar_t* wia_lstrcpyw(wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

typedef struct { const wchar_t* s; wchar_t* d; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; return (uint64_t)(size_t)wia_lstrcpyw(k->d, k->s); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; return (uint64_t)(size_t)sys(k->d, k->s); }
#pragma optimize("", on)

static wchar_t spool[16384];
static wchar_t dpool[16384];

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcpyW");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcpyW") : 0; }

    enum { N = 8 };
    /* lengths in CHARACTERS. The short rows are here because change 228 was parked for losing
       below ~32 bytes of work, and the same question has to be asked of this one honestly. */
    static const int LEN[N] = { 4, 8, 16, 32, 64, 254, 1024, 4000 };
    static const char* names[] = {"4 chars","8 chars","16 chars","32 chars",
                                  "64 chars","254 chars","1024 chars","4000 chars"};
    static CASE C[N]; static wia_case cs[N]; static size_t bytes[N];
    {
        int scur = 0, dcur = 0;
        for (int i = 0; i < N; ++i) {
            wchar_t* sp = spool + scur;
            for (int k = 0; k < LEN[i]; ++k) sp[k] = (wchar_t)(L'a' + k % 23);
            sp[LEN[i]] = 0;
            C[i].s = sp;
            C[i].d = dpool + dcur + 1;      /* +1: a deliberately ODD-aligned destination on half
                                               the cases, which is what the split-character clamp
                                               has to survive; the pool is wchar_t so this offsets
                                               by one CHARACTER, not one byte */
            if (i & 1) C[i].d = dpool + dcur;
            scur += LEN[i] + 32;
            dcur += LEN[i] + 32;
            if (scur > 16000 || dcur > 16000) { printf("BENCH SETUP ERROR\n"); return 1; }
            bytes[i] = (size_t)LEN[i] * sizeof(wchar_t);
            cs[i].label = names[i]; cs[i].bytes = bytes[i];
            cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
        }
    }
    return wia_bench_compare("kernelbase lstrcpyW (wia AVX2 page-clamped copy vs kernelbase SSE2)", cs, N, 300);
}
