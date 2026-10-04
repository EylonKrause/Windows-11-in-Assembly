/* changes/302-charupperw/bench.c
 * Gate 2: wia_charupperw / wia_charlowerw against the live user32 exports.
 *
 * Rows cover both modes and both of 277's paths: a single character (character mode, which is one
 * lookup either way and the row most likely to tie), ASCII strings from 8 to 4096 characters (the
 * vector path), a string with one Cyrillic unit in every 16 (every block takes the table), and an
 * all-Cyrillic string. Each case maps its own buffer in place on every call; after the first call the
 * text is already folded, which costs both implementations the same work, so the steady state is a
 * fair one.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

typedef LPWSTR (WINAPI *PFN)(LPWSTR);
extern LPWSTR wia_charupperw(LPWSTR);
extern LPWSTR wia_charlowerw(LPWSTR);
extern int wia_cuw_init(void);
static PFN sysU, sysL;

typedef struct { int dir; LPWSTR arg; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c) { CASE* k = (CASE*)c; return (uint64_t)(UINT_PTR)(k->dir ? wia_charlowerw(k->arg) : wia_charupperw(k->arg)); }
static uint64_t op_sys (void* c) { CASE* k = (CASE*)c; return (uint64_t)(UINT_PTR)(k->dir ? sysL(k->arg) : sysU(k->arg)); }
#pragma optimize("", on)

int main(void) {
    HMODULE u = LoadLibraryW(L"user32.dll");
    sysU = (PFN)GetProcAddress(u, "CharUpperW");
    sysL = (PFN)GetProcAddress(u, "CharLowerW");
    if (wia_cuw_init()) { printf("tables failed\n"); return 1; }

    struct { int dir; int len; int kind; const char* label; } R[] = {
        { 0,    0, 9, "upper: 1 char"      },
        { 0,    8, 0, "upper: 8 ascii"     },
        { 0,   64, 0, "upper: 64 ascii"    },
        { 0,  256, 0, "upper: 256 ascii"   },
        { 0, 1024, 0, "upper: 1024 ascii"  },
        { 0, 4096, 0, "upper: 4096 ascii"  },
        { 0,  256, 1, "upper: 256 mixed"   },
        { 0, 1024, 2, "upper: 1024 cyril." },
        { 1,    0, 9, "lower: 1 char"      },
        { 1,   64, 0, "lower: 64 ascii"    },
        { 1, 1024, 0, "lower: 1024 ascii"  },
        { 1, 1024, 2, "lower: 1024 cyril." },
    };
    enum { K = sizeof R / sizeof R[0] };
    static wchar_t bufs[K][4200];
    static CASE cs[K];
    static wia_case wc[K];
    for (int i = 0; i < K; ++i) {
        cs[i].dir = R[i].dir;
        if (R[i].kind == 9) {
            cs[i].arg = (LPWSTR)(UINT_PTR)(R[i].dir ? L'Q' : L'q');
        } else {
            for (int j = 0; j < R[i].len; ++j) {
                wchar_t c = (wchar_t)((R[i].dir ? L'A' : L'a') + j % 26);
                if (R[i].kind == 1 && j % 16 == 7) c = (wchar_t)0x0430;
                if (R[i].kind == 2) c = (wchar_t)((R[i].dir ? 0x0410 : 0x0430) + j % 32);
                bufs[i][j] = c;
            }
            bufs[i][R[i].len] = 0;
            cs[i].arg = bufs[i];
        }
        wc[i].label = R[i].label; wc[i].bytes = (size_t)(R[i].len ? R[i].len : 1) * 2;
        wc[i].ours = op_ours; wc[i].system = op_sys; wc[i].ctx = &cs[i];
    }
    return wia_bench_compare("user32 CharUpperW / CharLowerW  (wia: length first, then 277's map)", wc, K, 300);
}
