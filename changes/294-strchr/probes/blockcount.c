// changes/294-strchr/probes/blockcount.c -- driver for blockcount.asm. See that file's header.
//
// Chooses the number of 128-bit blocks in the prologue, and the width of the wide loop, by
// measuring the WORST (length, start-alignment) pair each variant produces against the live
// export -- because that worst pair is what the speed gate is, and it is not the pair `malloc`
// happens to hand a benchmark.
//
//   ml64 /nologo /c /Fo blockcountasm.obj blockcount.asm
//   cl /nologo /O2 /I ..\..\..\harness blockcount.c blockcountasm.obj /Fe:blockcount.exe
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"

typedef char* (__cdecl *fn)(const char*, int);
extern char* wia_n4(const char*,int);
extern char* wia_n5(const char*,int);
extern char* wia_n6(const char*,int);
extern char* wia_n8(const char*,int);
extern char* wia_n6y(const char*,int);
extern char* wia_n8y(const char*,int);
static fn sysu;

#define NV 6
static fn V[NV]         = { wia_n4, wia_n5, wia_n6, wia_n8, wia_n6y, wia_n8y };
static const char* VN[NV]={ "n4/512","n5/512","n6/512","n8/512","n6/256","n8/256" };

typedef struct { const char* s; int c; fn f; } ctx_t;
static uint64_t op(void* p){ ctx_t* x=(ctx_t*)p; return (uint64_t)(uintptr_t)x->f(x->s,x->c); }

static char* ref(const char* s, int c){
    unsigned char n=(unsigned char)(c&0xFF);
    for(;;){ if((unsigned char)*s==n) return (char*)s; if(!*s) return 0; ++s; }
}

static const int LEN[] = {16,24,31,40,47,49,55,63,64,72,80,96,112,128,160,255,1023,8191};
#define NL (int)(sizeof(LEN)/sizeof(LEN[0]))
static const int OFF[] = {0,5,16,31};
#define NO (int)(sizeof(OFF)/sizeof(OFF[0]))
#define REPS 3

static int cmpd(const void* a,const void* b){ double x=*(const double*)a,y=*(const double*)b; return x<y?-1:x>y; }

int main(void)
{
    sysu=(fn)GetProcAddress(LoadLibraryW(L"ucrtbase.dll"),"strchr");
    if(!sysu){ printf("resolve failed\n"); return 2; }

    // ---- screen every variant first: speed on a wrong answer is not a measurement -------------
    {
        static char b[8192]; unsigned long long r=0xABCDEF12345ULL;
        for(int it=0; it<80000; ++it){
            r=r*6364136223846793005ULL+1ULL;
            unsigned len=(unsigned)((r>>33)%400), off=(unsigned)((r>>13)%64);
            char* s=b+off;
            for(unsigned i=0;i<len;++i){ r=r*6364136223846793005ULL+1ULL; unsigned v=(unsigned)((r>>33)&0xFF); s[i]=(char)(v?v:9); }
            s[len]=0;
            r=r*6364136223846793005ULL+1ULL;
            int c=(int)((r>>33)&0xFF);
            if(((r>>20)&3)==0) c=0;
            if(len && ((r>>21)&3)==1) c=(unsigned char)s[(r>>25)%len];
            char* want=ref(s,c);
            if(sysu(s,c)!=want){ printf("SYS MISMATCH\n"); return 1; }
            for(int v=0;v<NV;++v) if(V[v](s,c)!=want){ printf("%s MISMATCH len=%u off=%u c=%d\n",VN[v],len,off,c); return 1; }
        }
        SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
        unsigned char* a=(unsigned char*)VirtualAlloc(NULL,pg*3,MEM_RESERVE,PAGE_NOACCESS);
        VirtualAlloc(a+pg,pg,MEM_COMMIT,PAGE_READWRITE);
        for(int tail=1;tail<=400;++tail){
            char* term=(char*)(a+2*pg-tail); char* s=term-500;
            memset(s,'A',500); *term=0;
            char* want=ref(s,'Q');
            for(int v=0;v<NV;++v) if(V[v](s,'Q')!=want){ printf("%s PAGE tail=%d\n",VN[v],tail); return 1; }
        }
        printf("variant screen: OK (80000 fuzz + 400 page-guard tails, all %d variants)\n\n",NV);
    }

    char* base=(char*)_aligned_malloc(16384,4096);
    volatile uint64_t sink=0;
    wia_pin(2);

    static double R[NV][NL][NO][REPS];
    static double S[NL][NO][REPS];
    for(int rep=0;rep<REPS;++rep)
      for(int i=0;i<NL;++i)
        for(int o=0;o<NO;++o){
            char* s=base+OFF[o];
            for(int j=0;j<LEN[i];++j) s[j]=(char)('a'+(j&15));
            s[LEN[i]]=0;
            ctx_t cs={s,'@',sysu};
            S[i][o][rep]=wia_measure(op,&cs,400,&sink);
            for(int v=0;v<NV;++v){ ctx_t cx={s,'@',V[v]}; R[v][i][o][rep]=wia_measure(op,&cx,400,&sink); }
        }

    printf("%6s |","len");
    for(int v=0;v<NV;++v) printf(" %8s",VN[v]);
    printf("      (worst ratio over the four start alignments, median of %d sweeps)\n",REPS);
    double worst[NV]; for(int v=0;v<NV;++v) worst[v]=1e300;
    for(int i=0;i<NL;++i){
        printf("%6d |",LEN[i]);
        for(int v=0;v<NV;++v){
            double w=1e300;
            for(int o=0;o<NO;++o){
                double rr[REPS];
                for(int k=0;k<REPS;++k) rr[k]=S[i][o][k]/R[v][i][o][k];
                qsort(rr,REPS,sizeof(double),cmpd);
                if(rr[REPS/2]<w) w=rr[REPS/2];
            }
            if(w<worst[v]) worst[v]=w;
            printf(" %8.3f",w);
        }
        printf("\n");
    }
    printf("%6s |","WORST");
    for(int v=0;v<NV;++v) printf(" %8.3f",worst[v]);
    printf("   <- the speed gate is this row\n");
    printf("sink=%llu\n",(unsigned long long)sink);
    return 0;
}
