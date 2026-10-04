/* changes/303-strcpyw/bench.c
 * Gate 2: wia_strcpyw / wia_strcatw against live shlwapi!StrCpyW / StrCatW.
 * Short rows are where the export is fastest relative to us (8.4 ns at 16 characters) and where a
 * block copy's setup could lose; the long rows are where its one-character loop costs 0.45 ns each.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef PWSTR (WINAPI *PFN)(PWSTR, PCWSTR);
extern PWSTR wia_strcpyw(PWSTR, PCWSTR);
extern PWSTR wia_strcatw(PWSTR, PCWSTR);
static PFN sysCpy, sysCat;

typedef struct { int cat; int dlen; wchar_t* d; const wchar_t* s; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c) { CASE* k = (CASE*)c; if (k->cat) k->d[k->dlen] = 0; return (uint64_t)(uintptr_t)(k->cat ? wia_strcatw(k->d, k->s) : wia_strcpyw(k->d, k->s)); }
static uint64_t op_sys (void* c) { CASE* k = (CASE*)c; if (k->cat) k->d[k->dlen] = 0; return (uint64_t)(uintptr_t)(k->cat ? sysCat(k->d, k->s) : sysCpy(k->d, k->s)); }
#pragma optimize("", on)

int main(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sysCpy = (PFN)GetProcAddress(h, "StrCpyW");
    sysCat = (PFN)GetProcAddress(h, "StrCatW");

    struct { int cat; int dlen; int slen; const char* label; } R[] = {
        { 0, 0,    1, "cpy 1"          }, { 0, 0,    8, "cpy 8"          }, { 0, 0,   16, "cpy 16"         },
        { 0, 0,   64, "cpy 64"         }, { 0, 0,  256, "cpy 256"        }, { 0, 0, 1024, "cpy 1024"       },
        { 0, 0, 4096, "cpy 4096"       },
        { 1, 16,  16, "cat 16+16"      }, { 1, 256, 256, "cat 256+256"   }, { 1, 4096, 4096, "cat 4096+4096" },
    };
    enum { K = sizeof R / sizeof R[0] };
    static wchar_t src[K][4200], dst[K][8400];
    static CASE cs[K];
    static wia_case wc[K];
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < R[i].slen; ++j) src[i][j] = (wchar_t)(L'a' + j % 26);
        src[i][R[i].slen] = 0;
        for (int j = 0; j < R[i].dlen; ++j) dst[i][j] = (wchar_t)(L'A' + j % 26);
        dst[i][R[i].dlen] = 0;
        cs[i].cat = R[i].cat; cs[i].dlen = R[i].dlen; cs[i].d = dst[i]; cs[i].s = src[i];
        wc[i].label = R[i].label; wc[i].bytes = (size_t)(R[i].dlen + R[i].slen) * 2;
        wc[i].ours = op_ours; wc[i].system = op_sys; wc[i].ctx = &cs[i];
    }
    return wia_bench_compare("shlwapi StrCpyW / StrCatW  (wia: page-bounded 32-byte blocks)", wc, K, 300);
}
