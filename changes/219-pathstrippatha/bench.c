// changes/219-pathstrippatha/bench.c
// Gate 2: time wia_pathstrippatha against the live shlwapi!PathStripPathA.
//
// The same classes change 162 used for the wide form, at the same CHARACTER counts, so the two are
// directly comparable. PathStripPathA mutates, so each iteration restores the input first and both
// sides pay the same copy. The last class puts the separator near the FRONT, which makes the move as
// long as it can be.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"
extern void wia_pathstrippatha(char*);
typedef void (WINAPI *fn)(char*);
static fn sys;
typedef struct { char* d; const char* seed; int len; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(size_t)(k->len+1));
    wia_pathstrippatha(k->d); return (uint64_t)k->d[0]; }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(size_t)(k->len+1));
    sys(k->d); return (uint64_t)k->d[0]; }
#pragma optimize("", on)
static char work[600];
static char s16[600], s64[600], s130[600], s254[600], s90[600], slate[600];
static void mk(char* b,int n,int every){ for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    if(every) for(int i=every;i<n;i+=every) b[i]='\\'; b[n]=0; }
int main(void){
    HMODULE sh=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(sh,"PathStripPathA");
    mk(s16,16,5); mk(s64,64,11); mk(s130,130,13); mk(s254,254,17);
    strcpy(s90, "C:\\Users\\Administrator\\Documents\\GitHub\\Windows-11-in-Assembly\\changes\\219\\impl.asm");
    mk(slate,254,0); slate[3]='\\';       /* separator near the front: the longest possible move */
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16 chars","64 chars","130 chars","254 chars",
                            "90-char real path","254, move from 4"};
    const char* S[]={s16,s64,s130,s254,s90,slate};
    static const int L[]={16,64,130,254,(int)0,254};
    for(int i=0;i<6;++i){
        C[i].d=work; C[i].seed=S[i];
        C[i].len=(int)strlen(S[i]);
        cs[i].label=N[i]; cs[i].bytes=(size_t)C[i].len;
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    (void)L;
    return wia_bench_compare("shlwapi PathStripPathA (wia AVX2 vs an MBCS per-character walk)", cs, 6, 300);
}
