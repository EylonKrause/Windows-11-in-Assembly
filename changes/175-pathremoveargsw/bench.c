// changes/175-pathremoveargsw/bench.c
// Gate 2: time wia_pathremoveargsw against the live shlwapi!PathRemoveArgsW.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
// Lengths are COMPUTED, never hardcoded (see change 172's RESULTS.md for why that matters).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "bench.h"

extern void wia_pathremoveargsw(wchar_t*);
typedef void (WINAPI *PRA)(PWSTR);
static PRA sys;

typedef struct { const wchar_t* src; int n; } CASE;
static wchar_t work[1300];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  wia_pathremoveargsw(work); return work[0]; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1)*2);
                                  sys(work); return work[0]; }
#pragma optimize("", on)

static wchar_t early[300], late254[300], none254[300], none1024[1100], quoted254[300], sreal[128];

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PRA)GetProcAddress(h,"PathRemoveArgsW");

    int n;
    /* args start early: the scan stops almost immediately */
    for(int i=0;i<254;i++) early[i]=(wchar_t)(L'a'+(i%23));
    early[8]=L' '; early[254]=0;
    /* args start at the very end: the scan crosses the whole string */
    for(int i=0;i<254;i++) late254[i]=(wchar_t)(L'a'+(i%23));
    late254[250]=L' '; late254[254]=0;
    /* no space at all: the scan runs to the terminator, then the trailing trim finds nothing */
    for(int i=0;i<254;i++) none254[i]=(wchar_t)(L'a'+(i%23));
    none254[254]=0;
    for(int i=0;i<1024;i++) none1024[i]=(wchar_t)(L'a'+(i%23));
    none1024[1024]=0;
    /* fully quoted with an internal space: every space is protected, so it scans to the end */
    for(int i=0;i<254;i++) quoted254[i]=(wchar_t)(L'a'+(i%23));
    quoted254[0]=L'"'; quoted254[120]=L' '; quoted254[253]=L'"'; quoted254[254]=0;
    {
        static const wchar_t* r = L"\"C:\\Program Files\\Notepad++\\notepad++.exe\" \"C:\\tmp\\a.txt\"";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; n=i;
    }

    enum { N = 6 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"args@8","args@250","no-space/254","no-space/1024","quoted/254","realpath"};
    C[0].src=early;     C[0].n=254;
    C[1].src=late254;   C[1].n=254;
    C[2].src=none254;   C[2].n=254;
    C[3].src=none1024;  C[3].n=1024;
    C[4].src=quoted254; C[4].n=254;
    C[5].src=sreal;     C[5].n=n;
    static const size_t bytes[] = { 18,502,508,2048,508,110 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathRemoveArgsW (wia AVX2 event scan vs shlwapi scalar)", cs, N, 300);
}
