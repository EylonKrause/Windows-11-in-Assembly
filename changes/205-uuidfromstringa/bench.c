// changes/205-uuidfromstringa/bench.c
// Gate 2: time wia_uuidfromstringa against the live rpcrt4!UuidFromStringA.
//
// The classes separate the paths that matter: the ordinary success parse (the only one a real caller
// hits in a loop), the two failure shapes a caller is most likely to feed it (a braced string, which
// this API rejects, and a truncated one), the NULL-pointer success case, and a mixed corpus so a
// perfectly-predicted single-input benchmark cannot flatter the result.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern long wia_uuidfromstringa(unsigned char*, GUID*);
typedef long (WINAPI *FN)(unsigned char*, GUID*);
static FN sys;

typedef struct { unsigned char* s; int mix; } CASE;
static GUID out;

#define MIXN 64
static unsigned char* MS[MIXN];
static char MBUF[MIXN][40];

#pragma optimize("", off)
static uint64_t op_ours(void* c){
    CASE* k = (CASE*)c;
    if (!k->mix) return (uint64_t)wia_uuidfromstringa(k->s, &out);
    uint64_t acc = 0;
    for (int i = 0; i < MIXN; ++i) acc += (uint64_t)wia_uuidfromstringa(MS[i], &out);
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

static unsigned char S_ok[]   = "deadbeef-1234-5678-9abc-def011223344";
static unsigned char S_up[]   = "DEADBEEF-1234-5678-9ABC-DEF011223344";
static unsigned char S_brace[]= "{deadbeef-1234-5678-9abc-def011223344}";
static unsigned char S_short[]= "deadbeef-1234-5678-9abc-def01122334";

int main(void){
    HMODULE h = LoadLibraryW(L"rpcrt4.dll");
    sys = (FN)GetProcAddress(h, "UuidFromStringA");

    {   /* the mixed corpus: three quarters valid, one quarter malformed in assorted ways */
        static const char HEX[] = "0123456789abcdefABCDEF";
        unsigned long sd = 0x2055u;
        for (int i = 0; i < MIXN; ++i) {
            for (int k = 0; k < 36; ++k) { sd = sd*1103515245u+12345u; MBUF[i][k] = HEX[(sd>>8)%22]; }
            MBUF[i][8] = MBUF[i][13] = MBUF[i][18] = MBUF[i][23] = '-';
            MBUF[i][36] = 0;
            if ((i & 3) == 3) MBUF[i][(i * 7) % 36] = '?';    /* one in four is invalid */
            MS[i] = (unsigned char*)MBUF[i];
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

    static const size_t bytes[] = { 36, 36, 38, 35, 1, 36 * MIXN };
    for (int i = 0; i < N; ++i) {
        cs[i].label = names[i]; cs[i].bytes = bytes[i];
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &C[i];
    }
    return wia_bench_compare("rpcrt4 UuidFromStringA (wia table parse vs a widen-then-parse wrapper)",
                             cs, N, 300);
}
