// changes/224-pathrenameextensiona/bench.c
// Gate 2: time wia_pathrenameexta against the live shlwapi!PathRenameExtensionA.
// In-place, so each iteration restores the buffer; both sides pay the same restore.
// Lengths are COMPUTED, never hardcoded -- a hardcoded length once cost a terminator and
// silently corrupted this project's measurements (see change 172's RESULTS.md).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern BOOL wia_pathrenameexta(char*, const char*);
typedef BOOL (WINAPI *PRE)(char*, const char*);
static PRE sys;

typedef struct { const char* src; int n; const char* ext; } CASE;
static char work[640];

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)wia_pathrenameexta(work, k->ext); }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; memcpy(work,k->src,(size_t)(k->n+1));
                                  return (uint64_t)sys(work, k->ext); }
#pragma optimize("", on)

static char s16[32], s64[96], s130[160], s254[300], sspc[300], snoext[300], sreal[128];

static int fill_ext(char* b,int n){             /* an ordinary path WITH an extension to replace */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n-4]='.'; b[n]=0; return n;
}
static int fill_noext(char* b,int n){           /* no extension: the scan runs to the terminator
                                                   and the extension is appended */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n]=0; return n;
}
static int fill_spc(char* b,int n){             /* a SPACE after the dot, so the dot is suppressed
                                                   and the second tracked position decides */
    for(int i=0;i<n;i++) b[i]=(char)('a'+(i%23));
    b[n-9]='.'; b[n-6]=' '; b[n]=0; return n;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PRE)GetProcAddress(h,"PathRenameExtensionA");
    int n16  = fill_ext(s16,16);
    int n64  = fill_ext(s64,64);
    int n130 = fill_ext(s130,130);
    int n254 = fill_ext(s254,254);
    int nsp  = fill_spc(sspc,254);
    int nne  = fill_noext(snoext,200);
    int nreal;
    {
        static const char* r = "C:\\Users\\eylonk\\Documents\\GitHub\\project\\source\\module.cpp";
        int i=0; for(; r[i]; ++i) sreal[i]=r[i]; sreal[i]=0; nreal=i;
    }

    enum { N = 7 };
    static CASE C[N]; static wia_case cs[N];
    static const char* names[] = {"16 chars","64 chars","130 chars","254 chars",
                                  "254, space after the dot","200, no extension","real path"};
    C[0].src=s16;     C[0].n=n16;  C[0].ext=".obj";
    C[1].src=s64;     C[1].n=n64;  C[1].ext=".obj";
    C[2].src=s130;    C[2].n=n130; C[2].ext=".obj";
    C[3].src=s254;    C[3].n=n254; C[3].ext=".obj";
    C[4].src=sspc;    C[4].n=nsp;  C[4].ext=".obj";
    C[5].src=snoext;  C[5].n=nne;  C[5].ext=".obj";
    C[6].src=sreal;   C[6].n=nreal;C[6].ext=".obj";
    static const size_t bytes[] = { 16,64,130,254,254,200,55 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&C[i]; }
    return wia_bench_compare("shlwapi PathRenameExtensionA (wia AVX2 fused scan vs shlwapi scalar)", cs, N, 300);
}
