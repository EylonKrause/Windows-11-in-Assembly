// changes/173-pathfindnextcomponentw/bench.c
// Gate 2: time wia_pathfindnextcomponentw against the live shlwapi!PathFindNextComponentW.
// Read-only, so no restore is needed and neither side pays a memcpy.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include "bench.h"

extern wchar_t* wia_pathfindnextcomponentw(const wchar_t*);
typedef PWSTR (WINAPI *PFNC)(PCWSTR);
static PFNC sys;

typedef struct { const wchar_t* s; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* c){ return (uint64_t)(UINT_PTR)wia_pathfindnextcomponentw(((CASE*)c)->s); }
static uint64_t op_sys (void* c){ return (uint64_t)(UINT_PTR)sys(((CASE*)c)->s); }
#pragma optimize("", on)

static wchar_t early[300], late64[96], late254[300], none254[300], none1024[1100], sreal[96];

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PFNC)GetProcAddress(h,"PathFindNextComponentW");

    /* separator early: the scan stops almost immediately */
    for(int i=0;i<254;i++) early[i]=(wchar_t)(L'a'+(i%23));
    early[2]=L'\\'; early[254]=0;
    /* separator at the end: the scan must cross the whole string */
    for(int i=0;i<64;i++) late64[i]=(wchar_t)(L'a'+(i%23));
    late64[62]=L'\\'; late64[64]=0;
    for(int i=0;i<254;i++) late254[i]=(wchar_t)(L'a'+(i%23));
    late254[252]=L'\\'; late254[254]=0;
    /* no separator at all: the scan runs to the terminator */
    for(int i=0;i<254;i++) none254[i]=(wchar_t)(L'a'+(i%23));
    none254[254]=0;
    for(int i=0;i<1024;i++) none1024[i]=(wchar_t)(L'a'+(i%23));
    none1024[1024]=0;
    {
        static const wchar_t* r = L"C:\\Program Files\\Windows NT\\Accessories\\wordpad.exe";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0;
    }

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"sep@2","sep@62/64","sep@252/254","no-sep/254","no-sep/1024","realpath"};
    C[0].s=early; C[1].s=late64; C[2].s=late254; C[3].s=none254; C[4].s=none1024; C[5].s=sreal;
    static const size_t bytes[] = { 6,126,506,508,2048,6 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathFindNextComponentW (wia AVX2 dual-compare vs shlwapi scalar)", cs, N, 300);
}
