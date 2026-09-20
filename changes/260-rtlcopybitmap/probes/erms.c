/* ermsprobe.c: is change 260's unshifted bulk copy losing to ERMS on Tiger Lake?
 *
 * On bench #3 change 260's byte-aligned 64 Kbit copy measures 338.58 ns against ntdll's 209.42
 * (0.62x), and the aligned and word-aligned rows lose too, while every SHIFTED row wins 9x-13x.
 * Every loss is a row where the shipped code reaches RtlCopyMemory and ours runs a 4x32-byte YMM
 * loop. Change 296 measured the `rep movsb` crossover on this exact part at ~2560-3072 bytes, and
 * 64 Kbit is 8192 bytes, well past it.
 *
 * This times the two loops directly, at the destination offsets the bench uses, so the fix is
 * chosen on a measurement rather than on the reasoning above.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>

static double f;
static double ns(void){ LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart*1e9/f; }

static unsigned char SRC[1u<<20], DST[1u<<20];

static void ymm4(unsigned char* d, const unsigned char* s, size_t n){
    size_t i=0, blocks=n>>5, q=blocks>>2, r=blocks&3;
    while(q--){ 
        __m256i a=_mm256_loadu_si256((const __m256i*)(s+i));
        __m256i b=_mm256_loadu_si256((const __m256i*)(s+i+32));
        __m256i c=_mm256_loadu_si256((const __m256i*)(s+i+64));
        __m256i e=_mm256_loadu_si256((const __m256i*)(s+i+96));
        _mm256_storeu_si256((__m256i*)(d+i),a);
        _mm256_storeu_si256((__m256i*)(d+i+32),b);
        _mm256_storeu_si256((__m256i*)(d+i+64),c);
        _mm256_storeu_si256((__m256i*)(d+i+96),e);
        i+=128;
    }
    while(r--){ __m256i a=_mm256_loadu_si256((const __m256i*)(s+i)); _mm256_storeu_si256((__m256i*)(d+i),a); i+=32; }
    if(i<n) memcpy(d+i,s+i,n-i);
}
static void erms(unsigned char* d, const unsigned char* s, size_t n){
    __movsb(d,s,n);
}

#define TRIALS 25
#define REPS   500
static double best(void (*fn)(unsigned char*,const unsigned char*,size_t),
                   int doff, int soff, size_t n){
    double b=1e30;
    for(int t=0;t<TRIALS;++t){
        double t0=ns();
        for(int r=0;r<REPS;++r) fn(DST+doff,SRC+soff,n);
        double dt=(ns()-t0)/REPS;
        if(dt<b) b=dt;
    }
    return b;
}

int main(void){
    LARGE_INTEGER q; QueryPerformanceFrequency(&q); f=(double)q.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(),1ull<<2);
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL);
    for(size_t i=0;i<sizeof SRC;++i) SRC[i]=(unsigned char)i;

    static const size_t N[]={ 64, 256, 1024, 2048, 2560, 3072, 4096, 8192, 32768 };
    static const struct { const char* tag; int doff, soff; } A[] = {
        { "aligned    (d0,s0)", 0, 0 },
        { "byte-align (d1,s0)", 1, 0 },
        { "word-align (d8,s0)", 8, 0 },
    };
    printf("%-20s %8s %10s %10s %8s\n","shape","bytes","4x YMM","rep movsb","movsb/ymm");
    printf("---------------------------------------------------------------\n");
    for(int a=0;a<3;++a){
        for(int k=0;k<9;++k){
            double y=best(ymm4,A[a].doff,A[a].soff,N[k]);
            double e=best(erms,A[a].doff,A[a].soff,N[k]);
            printf("%-20s %8zu %9.2f %10.2f %7.2fx%s\n",A[a].tag,N[k],y,e,y/e,
                   y/e>1.05?"  <= ERMS WINS":"");
        }
        printf("\n");
    }
    return 0;
}
