// changes/294-strchr/probes/smallband.c
//
// THE BAND THAT DECIDES THE VERDICT, measured on its own, PER START ALIGNMENT.
//
// bench.c's big rows are 3-4x wins and nothing about them is in doubt. The verdict rests on
// 16..160 bytes, where the whole call is a few nanoseconds and the margin over the shipped export
// is a percent or two. Three things this probe does that bench.c does not:
//
//   * it sweeps FOUR start alignments per length (0, 5, 16, 31 mod 32) and reports each one, not
//     just whatever `malloc` happened to hand back. Both implementations align the pointer DOWN,
//     so a given length lands on either side of a block boundary depending on where the caller's
//     string starts -- and bench.c, which uses malloc, sees only one of those cases per run.
//     This is how the 55..80-byte regression below was found at all;
//   * min-of-600 trials rather than 200, repeated, reporting the median of the repetitions;
//   * both ISA paths, because they enter the wide loop with different fixed costs.
//
//   cl /nologo /O2 /I ..\..\..\harness smallband.c ..\impl.obj /Fe:smallband.exe
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"

extern char* wia_strchr(const char*, int);
extern int   wia_strchr_set_path(int mode);
typedef char* (__cdecl *fn)(const char*, int);
static fn sysu;

typedef struct { const char* s; int c; } ctx_t;
static uint64_t op_ours(void* p){ ctx_t* x=(ctx_t*)p; return (uint64_t)(uintptr_t)wia_strchr(x->s,x->c); }
static uint64_t op_sys (void* p){ ctx_t* x=(ctx_t*)p; return (uint64_t)(uintptr_t)sysu(x->s,x->c); }

static const int LEN[] = {16,17,24,31,33,40,47,48,49,55,56,63,64,72,80,96,112,128,160,255};
#define NL (int)(sizeof(LEN)/sizeof(LEN[0]))
static const int OFF[] = {0,5,16,31};
#define NO (int)(sizeof(OFF)/sizeof(OFF[0]))
#define REPS 5

static int cmpd(const void* a, const void* b){ double x=*(const double*)a,y=*(const double*)b; return x<y?-1:x>y; }

static double R[NL][NO][REPS];

int main(void)
{
    sysu=(fn)GetProcAddress(LoadLibraryW(L"ucrtbase.dll"),"strchr");
    if(!sysu){ printf("resolve failed\n"); return 2; }
    char* base=(char*)_aligned_malloc(8192,4096);
    volatile uint64_t sink=0;
    wia_pin(2);

    int bad=0;
    for(int path=2; path>=1; --path){
        wia_strchr_set_path(path==1?1:0);
        for(int rep=0; rep<REPS; ++rep)
            for(int i=0;i<NL;++i)
                for(int o=0;o<NO;++o){
                    char* s=base+OFF[o];
                    for(int j=0;j<LEN[i];++j) s[j]=(char)('a'+(j&15));
                    s[LEN[i]]=0;
                    ctx_t cx={s,'@'};                       // absent: full scan, worst case
                    double a=wia_measure(op_ours,&cx,600,&sink);
                    double b=wia_measure(op_sys ,&cx,600,&sink);
                    R[i][o][rep]=b/a;
                }

        printf("\n=== %s : ratio vs live ucrtbase!strchr, median of %d sweeps, min-of-600 ===\n",
               path==1?"FORCED AVX2":"dispatched (AVX-512 here)", REPS);
        printf("%6s |","len");
        for(int o=0;o<NO;++o) printf(" off%-3d",OFF[o]);
        printf(" |   worst\n");
        for(int i=0;i<NL;++i){
            double worst=1e300;
            printf("%6d |",LEN[i]);
            for(int o=0;o<NO;++o){
                qsort(R[i][o],REPS,sizeof(double),cmpd);
                double md=R[i][o][REPS/2];
                if(md<worst) worst=md;
                printf(" %6.3f",md);
            }
            printf(" |  %6.3f %s\n", worst, worst<=0.97?"  <-- REGRESSION":"");
            if(worst<=0.97) ++bad;
        }
    }
    wia_strchr_set_path(0);
    printf("\n%s\nsink=%llu\n", bad? "SMALL BAND: a length/alignment regressed"
                                   : "SMALL BAND: no length regressed at any of the four alignments",
           (unsigned long long)sink);
    return bad?1:0;
}
