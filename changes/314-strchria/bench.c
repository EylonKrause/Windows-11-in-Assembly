/* changes/314-strchria/bench.c
 * Gate 2: StrChrIA and StrRChrIA against live shlwapi, through a monomorphic call site per
 * (function, side) -- change 304's call shape -- with REP calls per op on short rows (8) and one on
 * long ones, where a single export call is microseconds. Table ns are per REP calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef char* (WINAPI *PCHR)(const char*, WORD);
typedef char* (WINAPI *PRCHR)(const char*, const char*, WORD);
extern char* wia_strchria(const char*, WORD);
extern char* wia_strrchria(const char*, const char*, WORD);
int wia_sca_init(void);

typedef struct { const char* s; const char* e; WORD w; int rep; } arg;
#define SITEC(N) static PCHR volatile p_##N; static __declspec(noinline) uint64_t op_##N(void* c) { const arg* a = (const arg*)c; uint64_t acc = 0; for (int j = 0; j < a->rep; ++j) acc += (uintptr_t)p_##N(a->s, a->w); return acc; }
#define SITER(N) static PRCHR volatile p_##N; static __declspec(noinline) uint64_t op_##N(void* c) { const arg* a = (const arg*)c; uint64_t acc = 0; for (int j = 0; j < a->rep; ++j) acc += (uintptr_t)p_##N(a->s, a->e, a->w); return acc; }
SITEC(oc) SITEC(sc) SITER(or) SITER(sr)

int main(void) {
    if (!wia_sca_init()) { printf("init failed\n"); return 3; }
    HMODULE sh = LoadLibraryW(L"shlwapi.dll");
    p_sc = (PCHR)GetProcAddress(sh, "StrChrIA"); p_sr = (PRCHR)GetProcAddress(sh, "StrRChrIA");
    p_oc = wia_strchria; p_or = wia_strrchria;
    static char m16[17], m256[257], m4096[4097], acc[257];
    memset(m16, 'q', 16); memset(m256, 'q', 256); memset(m4096, 'q', 4096);
    for (int i = 0; i < 256; ++i) acc[i] = (char)(i % 2 ? 'E' : 'e');    /* e / E, alternating */
    acc[200] = (char)0xE9;                       /* e-acute: the only byte E-acute matches -- accents are not folded */
    static const char path[] = "C:\\Program Files\\Common Files\\Microsoft Shared\\VS7Debug\\vsjitdebugger.EXE";
    static arg A[12] = {
        { "Hello, World", 0, 'h', 8 },
        { "Hello, World", 0, 'D', 8 },
        { m16, 0, 'z', 8 },
        { m256, 0, 'z', 1 },
        { m4096, 0, 'z', 1 },
        { acc, 0, 0xC9, 1 },
        { path, 0, '\\', 8 },
        { m16, 0, 'z', 8 },
        { m256, 0, 'z', 1 },
        { m4096, 0, 'Q', 1 },
        { m4096, m4096 + 4096, 'z', 1 },
        { path, 0, '.', 8 },
    };
    static const char* nm[12] = { "ChrIA hit at 0 of 12", "ChrIA hit at 11 of 12", "ChrIA miss 16", "ChrIA miss 256", "ChrIA miss 4096",
                                  "ChrIA E-acute at 200", "ChrIA '\\' in a path",
                                  "RChrIA miss 16", "RChrIA miss 256", "RChrIA last of 4096", "RChrIA [s,s+4096) miss", "RChrIA '.' in a path" };
    static wia_case wc[12];
    for (int i = 0; i < 12; ++i) {
        int r = i >= 7;
        wc[i].label = nm[i]; wc[i].bytes = A[i].e ? (size_t)(A[i].e - A[i].s) : strlen(A[i].s);
        wc[i].ours = r ? op_or : op_oc; wc[i].system = r ? op_sr : op_sc; wc[i].ctx = &A[i];
    }
    return wia_bench_compare("shlwapi StrChrIA / StrRChrIA  (wia: two-member match sets, 32-byte compares; ns per op = 8 calls short, 1 long)", wc, 12, 40);
}
