// changes/160-pathcchaddextension/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>
#include "bench.h"
extern HRESULT wia_pathcchaddext(wchar_t*, size_t, const wchar_t*);
typedef HRESULT (WINAPI *fn)(PWSTR, size_t, PCWSTR);
static fn sys;
typedef struct { wchar_t* d; size_t cch; const wchar_t* seed; int len; const wchar_t* ext; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(k->len+1)*2);
    return (uint64_t)(unsigned)wia_pathcchaddext(k->d,k->cch,k->ext); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(k->len+1)*2);
    return (uint64_t)(unsigned)sys(k->d,k->cch,k->ext); }
#pragma optimize("", on)
static wchar_t work[600];
static wchar_t s16[600], s64[600], s130[600], s254[600], s90[600], snoext[600];
static void mk(wchar_t* b,int n,int dot){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23));
    for(int i=0;i<n;i+=17) b[i]=L'\\'; if(dot>=0) b[dot]=L'.'; b[n]=0; }
int main(void){
    HMODULE kb=LoadLibraryW(L"kernelbase.dll"); sys=(fn)GetProcAddress(kb,"PathCchAddExtension");
    mk(s16,16,-1); mk(s64,64,-1); mk(s130,130,-1); mk(s254,254,-1);
    wcscpy(s90, L"C:\\Users\\Administrator\\Documents\\GitHub\\Windows-11-in-Assembly\\changes\\159\\impl.asm");
    mk(snoext,200,196);   /* this one returns S_FALSE: the early-out path */
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16 chars","64 chars","130 chars","254 chars",
                            "90-char real path","200 chars, already has one"};
    const wchar_t* S[]={s16,s64,s130,s254,s90,snoext};
    for(int i=0;i<6;++i){
        C[i].d=work; C[i].cch=520; C[i].seed=S[i]; C[i].len=(int)wcslen(S[i]); C[i].ext=L".obj";
        cs[i].label=N[i]; cs[i].bytes=C[i].len*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    return wia_bench_compare("kernelbase PathCchAddExtension (wia AVX2 vs kernelbase)", cs, 6, 300);
}
