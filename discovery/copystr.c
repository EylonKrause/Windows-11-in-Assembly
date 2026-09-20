/* copystr.c -- is ntdll!RtlCopyString actually beatable on this part, measured like-for-like?
 *
 * Tier 5 put RtlCopyString at 66.25 ns for 4096 bytes and RtlAppendStringToString at 65.55, and
 * called them targets by comparing against a DIFFERENT probe (260's erms.c) that timed rep movsb
 * at 26.20 ns for the same 4096 bytes. Two probes, two harnesses, two runs -- which is exactly the
 * kind of cross-referencing this repository keeps catching itself doing.
 *
 * So this times them SIDE BY SIDE in one harness, on the same buffers, in the same run: the live
 * export against the two things a replacement could actually be (a 4x32-byte YMM loop and
 * rep movsb), at every size a threshold would have to choose between.
 *
 * It also answers the question the implementation needs: WHERE is the crossover, for THIS call
 * shape, including the function-call and STRING-header overhead the raw memcpy probe does not pay.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>

typedef struct { USHORT Length, MaximumLength; CHAR* Buffer; } ASTR;
typedef void (NTAPI *pfn_CopyStr)(ASTR*, const ASTR*);
typedef LONG (NTAPI *pfn_AppendStr)(ASTR*, const ASTR*);

static double qf;
static void tinit(void){ LARGE_INTEGER f; QueryPerformanceFrequency(&f); qf=(double)f.QuadPart; }
static double nowns(void){ LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart*1e9/qf; }

static char SRC[70000], DST[70000];
static volatile uint64_t sink;

/* what a replacement would do without ERMS: the 4x32 loop 260's parent uses */
static void ymm4(char* d, const char* s, size_t n){
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

#define TRIALS 25
#define REPS   2000
/* MSVC has no statement expressions, so the macro assigns instead of returning. */
#define BEST(out, body) do{ double best_=1e30; int t_,r_; \
    for(t_=0;t_<TRIALS;++t_){ double t0_=nowns(); for(r_=0;r_<REPS;++r_){ body; } \
      { double dt_=(nowns()-t0_)/REPS; if(dt_<best_) best_=dt_; } } (out)=best_; }while(0)

int main(void){
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    pfn_CopyStr   pCpS = (pfn_CopyStr)GetProcAddress(h,"RtlCopyString");
    pfn_AppendStr pApp = (pfn_AppendStr)GetProcAddress(h,"RtlAppendStringToString");
    static const int N[] = { 16, 64, 256, 512, 1024, 1536, 2048, 3072, 4096, 8192, 16384, 32000 };
    int k, i;

    tinit();
    SetThreadAffinityMask(GetCurrentThread(), 1ull<<2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for(i=0;i<(int)sizeof SRC;++i) SRC[i]=(char)('a'+(i&15));
    if(!pCpS || !pApp){ printf("cannot resolve the exports\n"); return 2; }

    printf("ntdll!RtlCopyString against what a replacement could be, same harness, same run.\n");
    printf("%8s %12s %12s %12s   %10s %10s\n","bytes","RtlCopyStr","4x32 YMM","rep movsb","ntdll/ymm","ntdll/movsb");
    printf("--------------------------------------------------------------------------------\n");
    for(k=0;k<12;++k){
        int n=N[k];
        ASTR src, dst;
        double a,b,c;
        src.Length=(USHORT)n; src.MaximumLength=(USHORT)n; src.Buffer=SRC;
        dst.Length=0; dst.MaximumLength=(USHORT)60000; dst.Buffer=DST;
        BEST(a, { pCpS(&dst,&src); sink+=dst.Length; });
        BEST(b, { ymm4(DST,SRC,(size_t)n); sink+=(uint64_t)DST[0]; });
        BEST(c, { __movsb((unsigned char*)DST,(const unsigned char*)SRC,(size_t)n); sink+=(uint64_t)DST[0]; });
        printf("%8d %12.2f %12.2f %12.2f   %9.2fx %10.2fx%s\n", n, a, b, c, a/b, a/c,
               (a/c > 1.15) ? "   <= worth a change" : "");
    }

    printf("\nand the append form, which shares the copy:\n");
    printf("%8s %12s %12s\n","bytes","RtlAppendStr","rep movsb");
    printf("--------------------------------------------------\n");
    for(k=0;k<12;++k){
        int n=N[k];
        ASTR src, dst;
        double a,c;
        src.Length=(USHORT)n; src.MaximumLength=(USHORT)n; src.Buffer=SRC;
        dst.Length=0; dst.MaximumLength=(USHORT)60000; dst.Buffer=DST;
        BEST(a, { dst.Length=0; sink+=(ULONG)pApp(&dst,&src); });
        BEST(c, { __movsb((unsigned char*)DST,(const unsigned char*)SRC,(size_t)n); sink+=(uint64_t)DST[0]; });
        printf("%8d %12.2f %12.2f   %9.2fx\n", n, a, c, a/c);
    }
    printf("\nsink=%llu\n",(unsigned long long)sink);
    return 0;
}
