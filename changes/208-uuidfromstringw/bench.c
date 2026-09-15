// changes/208-uuidfromstringw/bench.c
// Gate 2: time wia_uuidfromstringw against the live rpcrt4!UuidFromStringW.
//
// The classes separate the paths that matter: the ordinary success parse (the only one a real caller
// hits in a loop), the two failure shapes a caller is most likely to feed it (a braced string, which
// this API rejects, and a truncated one), the NULL-pointer success case, and a mixed corpus so a
// perfectly-predicted single-input benchmark cannot flatter the result.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern long wia_uuidfromstringw(wchar_t*, GUID*);
typedef long (WINAPI *FN)(wchar_t*, GUID*);
static FN sys;

typedef struct { wchar_t* s; int mix; } CASE;
static GUID out;

#define MIXN 64
static wchar_t* MS[MIXN];
static wchar_t MBUF[MIXN][40];

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    if (!k->mix) return (uint64_t)wia_uuidfromstringw(k->s, &out);
    uint64_t acc = 0;
    for (int i = 0; i < MIXN; ++i) acc += (uint64_t)wia_uuidfromstringw(MS[i], &out);
    return acc;
}
static uint64_t op_sys(void* c){
    CASE* k = (CASE*)c;
    if (!k->mix) return (uint64_t)sys(k->s, &out);
    uint64_t acc = 0;
    for (int i = 0; i < MIXN; ++i) acc += (uint64_t)sys(MS[i], &out);
    return acc;
}
#pragma optimize("", on)

static wchar_t S_ok[]    = L"deadbeef-1234-5678-9abc-def011223344";
static wchar_t S_up[]    = L"DEADBEEF-1234-5678-9ABC-DEF011223344";
static wchar_t S_brace[] = L"{deadbeef-1234-5678-9abc-def011223344}";
static wchar_t S_short[] = L"deadbeef-1234-5678-9abc-def01122334";

int main(void){
    HMODULE h = LoadLibraryW(L"rpcrt4.dll");
    sys = (FN)GetProcAddress(h, "UuidFromStringW");

    {   /* the mixed corpus: three quarters valid, one quarter malformed in assorted ways */
        static const wchar_t HEX[] = L"0123456789abcdefABCDEF";
        unsigned long sd = 0x2055u;
        for (int i = 0; i < MIXN; ++i) {
            for (int k = 0; k < 36; ++k) { sd = sd*1103515245u+12345u; MBUF[i][k] = HEX[(sd>>8)%22]; }
            MBUF[i][8] = MBUF[i][13] = MBUF[i][18] = MBUF[i][23] = L'-';
            MBUF[i][36] = 0;
            if ((i & 3) == 3) MBUF[i][(i * 7) % 36] = L'?';    /* one in four is invalid */
            MS[i] = (wchar_t*)MBUF[i];
        }
    }

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {
        "valid lower", "valid UPPER", "braced (rejected)", "truncated (rejected)",
        "NULL (nil uuid)", "mixed x64" };
    C[0].s = S_ok;
    C[1].s = S_up;
    C[2].s = S_brace;
    C[3].s = S_short;
    C[4].s = NULL;
    C[5].mix = 1;

    static const size_t bytes[] = { 72, 72, 76, 70, 1, 72 * MIXN };
    for (int i = 0; i < N; ++i) {
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("rpcrt4 UuidFromStringW (wia saturating narrow + table parse vs a scalar parse)",
                             cs, N, 300);
}
