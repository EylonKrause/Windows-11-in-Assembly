// changes/207-iidfromstring/bench.c
// Gate 2: time wia_iidfromstring against the live combase!IIDFromString.
//
// The shipped function costs the same whether the string parses or not (32.96 vs 33.14 ns), because
// its 38-character strlen runs first either way. The classes below therefore separate OUR paths: a
// valid parse, a content rejection that stops early, a content rejection that stops late, and a
// structural rejection that never reaches the parser at all.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern long wia_iidfromstring(const wchar_t*, GUID*);
typedef HRESULT (WINAPI *FN)(const wchar_t*, GUID*);
static FN sys;

typedef struct { const wchar_t* s; int mix; } CASE;
static GUID out;

#define MIXN 64
static const wchar_t* MS[MIXN];
static wchar_t MBUF[MIXN][48];

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    if (!k->mix) return (uint64_t)(unsigned long)wia_iidfromstring(k->s, &out);
    uint64_t acc = 0;
    for (int i = 0; i < MIXN; ++i) acc += (uint64_t)(unsigned long)wia_iidfromstring(MS[i], &out);
    return acc;
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    if (!k->mix) return (uint64_t)(unsigned long)sys(k->s, &out);
    uint64_t acc = 0;
    for (int i = 0; i < MIXN; ++i) acc += (uint64_t)(unsigned long)sys(MS[i], &out);
    return acc;
}
#pragma optimize("", on)

int main(void){
    HMODULE h = LoadLibraryW(L"combase.dll");
    sys = (FN)GetProcAddress(h, "IIDFromString");
    if(!sys){ h = LoadLibraryW(L"ole32.dll"); sys = (FN)GetProcAddress(h,"IIDFromString"); }

    {   /* three quarters valid, one quarter rejected at assorted depths */
        static const wchar_t HEX[] = L"0123456789abcdefABCDEF";
        unsigned long sd = 0x2077u;
        for (int i = 0; i < MIXN; ++i) {
            MBUF[i][0] = L'{';
            for (int k = 0; k < 36; ++k) { sd = sd*1103515245u+12345u; MBUF[i][1+k] = HEX[(sd>>8)%22]; }
            MBUF[i][9] = MBUF[i][14] = MBUF[i][19] = MBUF[i][24] = L'-';
            MBUF[i][37] = L'}'; MBUF[i][38] = 0;
            if ((i & 3) == 3) MBUF[i][1 + (i * 7) % 36] = L'?';
            MS[i] = MBUF[i];
        }
    }

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "valid UPPER", "valid lower", "bad digit at 1 (early)", "bad digit at 36 (late)",
        "wrong length (structural)", "mixed x64" };
    C[0].s = L"{DEADBEEF-1234-5678-9ABC-DEF011223344}";
    C[1].s = L"{deadbeef-1234-5678-9abc-def011223344}";
    C[2].s = L"{ZEADBEEF-1234-5678-9ABC-DEF011223344}";
    C[3].s = L"{DEADBEEF-1234-5678-9ABC-DEF01122334Z}";
    C[4].s = L"{DEADBEEF-1234-5678-9ABC-DEF01122334}";
    C[5].mix = 1;

    static const size_t bytes[] = { 38, 38, 38, 38, 37, 38 * MIXN };
    for (int i = 0; i < N; ++i) {
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("combase IIDFromString (wia AVX2 length + table parse vs 3 compares a digit)",
                             cs, N, 300);
}
