/* changes/313-strtointexa/bench.c
 * Gate 2: StrToInt64ExA and StrToIntExA against live shlwapi. Each timed op is REP calls on the same
 * string through a monomorphic call site per (function, side) -- change 304's call shape. Rows are
 * the shapes a caller hands these: a digit, a 10-digit number, a signed one with leading blanks, hex,
 * the 64-bit maximum, a number with text after it, no number at all, and strings past the export's
 * 260-unit stack buffer (300 bytes, 5,000 bytes). Table ns are per REP calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef BOOL (WINAPI *P64)(const char*, DWORD, LONGLONG*);
typedef BOOL (WINAPI *P32)(const char*, DWORD, int*);
extern BOOL wia_strtoint64exa(const char*, DWORD, LONGLONG*);
extern BOOL wia_strtointexa(const char*, DWORD, int*);
int wia_sti_init(void);

#define REP 8
typedef struct { const char* s; DWORD f; } arg;
#define SITE64(N) static P64 volatile p_##N; static __declspec(noinline) uint64_t op_##N(void* c) { const arg* a = (const arg*)c; uint64_t acc = 0; LONGLONG v; for (int j = 0; j < REP; ++j) { acc += p_##N(a->s, a->f, &v); acc += (uint64_t)v; } return acc; }
#define SITE32(N) static P32 volatile p_##N; static __declspec(noinline) uint64_t op_##N(void* c) { const arg* a = (const arg*)c; uint64_t acc = 0; int v; for (int j = 0; j < REP; ++j) { acc += p_##N(a->s, a->f, &v); acc += (uint64_t)v; } return acc; }
SITE64(o64) SITE64(s64) SITE32(o32) SITE32(s32)

int main(void) {
    if (!wia_sti_init()) { printf("init failed\n"); return 3; }
    HMODULE sh = LoadLibraryW(L"shlwapi.dll");
    p_s64 = (P64)GetProcAddress(sh, "StrToInt64ExA"); p_s32 = (P32)GetProcAddress(sh, "StrToIntExA");
    p_o64 = wia_strtoint64exa; p_o32 = wia_strtointexa;
    static char s300[301], s5000[5001];
    memset(s300, 'z', 300); memcpy(s300, "4096 ", 5);
    memset(s5000, 'z', 5000); memcpy(s5000, "  -123 ", 7);
    static const arg A[9] = {
        { "7", 0 }, { "1234567890", 0 }, { "  -2147483647", 0 }, { "0x7FFFFFFF", 1 }, { "9223372036854775807", 0 },
        { "12 and some text after the number, as in a config line", 0 }, { "none", 0 }, { s300, 0 }, { s5000, 0 },
    };
    static const char* nm[9] = { "\"7\"", "10 digits", "\"  -2147483647\"", "hex 0x7FFFFFFF", "INT64_MAX",
                                 "12 + 52 bytes of text", "no number", "300 bytes", "5000 bytes" };
    static wia_case wc[18]; static char lab[18][48];
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < 9; ++i) {
            int r = k * 9 + i;
            sprintf(lab[r], "%s %s", k ? "IntExA" : "Int64ExA", nm[i]);
            wc[r].label = lab[r]; wc[r].bytes = (int)strlen(A[i].s);
            wc[r].ours = k ? op_o32 : op_o64; wc[r].system = k ? op_s32 : op_s64;
            wc[r].ctx = (void*)&A[i];
        }
    return wia_bench_compare("shlwapi StrToInt64ExA / StrToIntExA  (wia: parse the bytes in place + page-bounded NUL scan; ns per 8 calls)", wc, 18, 300);
}
