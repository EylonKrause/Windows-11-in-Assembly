// changes/002-memchr/bench.c: wia_memchr vs live ucrtbase memchr.
// Worst case for a find: target ABSENT, so every byte is scanned.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

extern void* wia_memchr(const void*, int, size_t);
typedef void* (__cdecl *memchr_fn)(const void*, int, size_t);
static memchr_fn sys_memchr;

typedef struct { const void* p; size_t n; } memctx;
static uint64_t op_ours(void* c)   { memctx* m=(memctx*)c; return (uint64_t)(uintptr_t)wia_memchr(m->p,0x55,m->n); }
static uint64_t op_system(void* c) { memctx* m=(memctx*)c; return (uint64_t)(uintptr_t)sys_memchr(m->p,0x55,m->n); }

int main(void) {
    HMODULE h = LoadLibraryW(L"ucrtbase.dll");
    sys_memchr = (memchr_fn)GetProcAddress(h, "memchr");

    static const size_t ns[] = { 8, 32, 128, 1024, 8192, 65536, 1048576 };
    static const char*  nm[] = { "8", "32", "128", "1KB", "8KB", "64KB", "1MB" };
    enum { N = 7 };
    static memctx ctx[N];
    static wia_case cases[N];
    for (int i = 0; i < N; ++i) {
        unsigned char* b = (unsigned char*)malloc(ns[i]);
        for (size_t k = 0; k < ns[i]; ++k) b[k] = 0xAA;   // target 0x55 absent
        ctx[i].p = b; ctx[i].n = ns[i];
        cases[i].label = nm[i]; cases[i].bytes = ns[i];
        cases[i].ours = op_ours; cases[i].system = op_system; cases[i].ctx = &ctx[i];
    }
    return wia_bench_compare("memchr  (wia AVX2 vs ucrtbase, target absent)", cases, N, 200);
}
