// changes/164-pathcchaddbackslash/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>
#include "bench.h"
extern HRESULT wia_pathcchaddbackslash(wchar_t*, size_t);
typedef HRESULT (WINAPI *fn)(PWSTR, size_t);
static fn sys;
typedef struct { wchar_t* d; size_t cch; const wchar_t* seed; int len; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(k->len+1)*2);
    return (uint64_t)(unsigned)wia_pathcchaddbackslash(k->d,k->cch); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(k->len+1)*2);
    return (uint64_t)(unsigned)sys(k->d,k->cch); }
#pragma optimize("", on)
static wchar_t work[600];
static wchar_t s16[600], s64[600], s130[600], s254[600], s90[600], shas[600];
static void mk(wchar_t* b,int n){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23)); b[n]=0; }
int main(void){
    HMODULE kb=LoadLibraryW(L"kernelbase.dll"); sys=(fn)GetProcAddress(kb,"PathCchAddBackslash");
    mk(s16,16); mk(s64,64); mk(s130,130); mk(s254,254);
    wcscpy(s90, L"C:\\Users\\Administrator\\Documents\\GitHub\\Windows-11-in-Assembly\\changes\\164");
    mk(shas,254); shas[253]=L'\\';        /* already ends with one: the S_FALSE path */
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16 chars","64 chars","130 chars","254 chars",
                            "70-char real path","254 chars, already ends with one"};
    const wchar_t* S[]={s16,s64,s130,s254,s90,shas};
    for(int i=0;i<6;++i){
        C[i].d=work; C[i].cch=520; C[i].seed=S[i]; C[i].len=(int)wcslen(S[i]);
        cs[i].label=N[i]; cs[i].bytes=C[i].len*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    return wia_bench_compare("kernelbase PathCchAddBackslash (wia AVX2 vs kernelbase)", cs, 6, 300);
}
