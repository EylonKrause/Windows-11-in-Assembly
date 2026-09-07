// changes/162-pathstrippathw/bench.c
// Each op restores the path first, since the routine mutates it. That copy is paid identically by
// both sides.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>
#include "bench.h"
extern void wia_pathstrippathw(wchar_t*);
typedef void (WINAPI *fn)(PWSTR);
static fn sys;
typedef struct { wchar_t* d; const wchar_t* seed; int len; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(k->len+1)*2);
    wia_pathstrippathw(k->d); return (uint64_t)k->d[0]; }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(k->len+1)*2);
    sys(k->d); return (uint64_t)k->d[0]; }
#pragma optimize("", on)
static wchar_t work[600];
static wchar_t s16[600], s64[600], s130[600], s254[600], s90[600], slate[600];
static void mk(wchar_t* b,int n,int every){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23));
    if(every) for(int i=every;i<n;i+=every) b[i]=L'\\'; b[n]=0; }
int main(void){
    HMODULE sh=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(sh,"PathStripPathW");
    mk(s16,16,5); mk(s64,64,11); mk(s130,130,13); mk(s254,254,17);
    wcscpy(s90, L"C:\\Users\\Administrator\\Documents\\GitHub\\Windows-11-in-Assembly\\changes\\162\\impl.asm");
    mk(slate,254,0); slate[3]=L'\\';       /* separator near the front: the longest possible move */
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16 chars","64 chars","130 chars","254 chars",
                            "90-char real path","254 chars, separator at 3 (longest move)"};
    const wchar_t* S[]={s16,s64,s130,s254,s90,slate};
    for(int i=0;i<6;++i){
        C[i].d=work; C[i].seed=S[i]; C[i].len=(int)wcslen(S[i]);
        cs[i].label=N[i]; cs[i].bytes=C[i].len*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    return wia_bench_compare("shlwapi PathStripPathW (wia AVX2 vs shlwapi)", cs, 6, 300);
}
