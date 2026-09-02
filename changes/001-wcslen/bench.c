// changes/001-wcslen/bench.c
// Gate 2: time wia_wcslen vs the live system wcslen across size classes and
// print a BETTER/WORSE/tie verdict per class + overall.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include "bench.h"

extern size_t wia_wcslen(const wchar_t*);
typedef size_t (__cdecl *wcslen_fn)(const wchar_t*);
static wcslen_fn sys_wcslen;

typedef struct { const wchar_t* s; } strctx;

static uint64_t op_ours(void* c)   { return (uint64_t)wia_wcslen(((strctx*)c)->s); }
static uint64_t op_system(void* c) { return (uint64_t)sys_wcslen(((strctx*)c)->s); }

static const wchar_t* make_str(size_t len) {
    wchar_t* s = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    for (size_t i = 0; i < len; ++i) s[i] = L'a' + (wchar_t)(i & 15);
    s[len] = 0;
    return s;
}

int main(void) {
    HMODULE h = LoadLibraryW(L"ucrtbase.dll");
    sys_wcslen = (wcslen_fn)GetProcAddress(h, "wcslen");

    static const size_t lens[] = { 3, 15, 63, 255, 1023, 8191, 65535 };
    static const char*  names[] = { "3", "15", "63", "255", "1023", "8191", "65535" };
    enum { N = 7 };
    static strctx ctx[N];
    static wia_case cases[N];
    for (int i = 0; i < N; ++i) {
        ctx[i].s = make_str(lens[i]);
        cases[i].label  = names[i];
        cases[i].bytes  = lens[i] * 2;      // bytes scanned
        cases[i].ours   = op_ours;
        cases[i].system = op_system;
        cases[i].ctx    = &ctx[i];
    }
    // inner small so a huge-string call still fits; min-of-trials cleans noise.
    return wia_bench_compare("wcslen  (wia AVX2 vs ucrtbase)", cases, N, 200);
}
