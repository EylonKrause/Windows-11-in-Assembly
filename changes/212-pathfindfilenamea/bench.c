// changes/212-pathfindfilenamea/bench.c
// Gate 2: time wia_pathfindfilenamea against the live shlwapi!PathFindFileNameA.
//
// Same classes as change 161 used for the wide form, at the same CHARACTER counts, so the two are
// directly comparable. The "no separators" class is the one that exercises whole-block skipping; the
// 90-character real path is the shape actual callers pass.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern const char* wia_pathfindfilenamea(const char*);
typedef char* (WINAPI *fn)(const char*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ return (uint64_t)(size_t)wia_pathfindfilenamea((const char*)x); }
static uint64_t op_sys (void* x){ return (uint64_t)(size_t)sys((const char*)x); }
#pragma optimize("", on)
static char s16[400], s64[400], s130[400], s254[400], s90[400], sflat[400];
static void mk(char* b,int n,int every){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    if(every) for(int i=every;i<n;i+=every) b[i]='\\'; b[n]=0; }
int main(void){
    HMODULE sh=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(sh,"PathFindFileNameA");
    mk(s16,16,5); mk(s64,64,11); mk(s130,130,13); mk(s254,254,17);
    strcpy(s90, "C:\\Users\\Administrator\\Documents\\GitHub\\Windows-11-in-Assembly\\changes\\212\\impl.asm");
    mk(sflat,254,0);        /* no separators at all: the whole string is one component */
    static const char* N[]={"16 chars","64 chars","130 chars","254 chars",
                            "90-char real path","254 chars, no separators"};
    const char* S[]={s16,s64,s130,s254,s90,sflat};
    static const int L[]={16,64,130,254,90,254};
    static wia_case cs[6];
    for(int i=0;i<6;++i){ cs[i].label=N[i]; cs[i].bytes=L[i]; cs[i].ours=op_ours;
                          cs[i].system=op_sys; cs[i].ctx=(void*)S[i]; }
    return wia_bench_compare("shlwapi PathFindFileNameA (wia AVX2 vs an MBCS-aware per-character walk)", cs, 6, 300);
}
