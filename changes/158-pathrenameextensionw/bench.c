// changes/158-pathrenameextensionw/bench.c
// Each op restores the path first, so every iteration renames the same starting string. That copy is
// paid identically by both sides.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>
#include "bench.h"
extern BOOL wia_pathrenameextw(wchar_t*, const wchar_t*);
typedef BOOL (WINAPI *fn)(PWSTR, PCWSTR);
static fn sys;
typedef struct { wchar_t* d; const wchar_t* seed; int len; const wchar_t* ext; } CASE;
#pragma optimize("", off)
static uint64_t op_ours(void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(k->len+1)*2);
    return (uint64_t)(unsigned)wia_pathrenameextw(k->d,k->ext); }
static uint64_t op_sys (void* x){ CASE* k=(CASE*)x; memcpy(k->d,k->seed,(k->len+1)*2);
    return (uint64_t)(unsigned)sys(k->d,k->ext); }
#pragma optimize("", on)
static wchar_t work[400];
static wchar_t s16[400], s64[400], s130[400], s254[400], s90[400], snoext[400];
static void mk(wchar_t* b,int n,int dot){ for(int i=0;i<n;i++) b[i]=(wchar_t)(L'a'+(i%23));
    for(int i=0;i<n;i+=17) b[i]=L'\\'; if(dot>=0) b[dot]=L'.'; b[n]=0; }
int main(void){
    HMODULE sh=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(sh,"PathRenameExtensionW");
    mk(s16,16,12); mk(s64,64,60); mk(s130,130,126); mk(s254,254,250);
    wcscpy(s90, L"C:\\Users\\Administrator\\Documents\\GitHub\\Windows-11-in-Assembly\\changes\\158\\impl.asm");
    mk(snoext,200,-1);
    static CASE C[6]; static wia_case cs[6];
    static const char* N[]={"16 chars","64 chars","130 chars","254 chars",
                            "90-char real path","200 chars, no extension"};
    static const int L[]={16,64,130,254,0,200};
    const wchar_t* S[]={s16,s64,s130,s254,s90,snoext};
    for(int i=0;i<6;++i){
        C[i].d=work; C[i].seed=S[i]; C[i].len=(int)wcslen(S[i]); C[i].ext=L".obj";
        cs[i].label=N[i]; cs[i].bytes=C[i].len*2; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i];
    }
    (void)L;
    return wia_bench_compare("shlwapi PathRenameExtensionW (wia AVX2 vs shlwapi)", cs, 6, 300);
}
