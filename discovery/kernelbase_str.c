/* discovery/kernelbase_str.c
   The rest of the kernelbase lstr* family, surveyed the way change 225 found lstrlenA.

   WHY. discovery/shlwapi_narrow.c timed lstrlenA against lstrlenW on the same subject and the
   NARROW one came out four times slower PER BYTE -- 21.7 bytes/ns against 84.8. That was not an
   MBCS tax; it was a 16-byte SSE2 loop against a 32-byte AVX2 one, and change 225 took it to
   158.7 GB/s. The obvious question is whether the rest of the family carries the same gap.

   These are among the hottest functions in the Win32 surface and NONE of them is converted yet --
   the kernelbase folder of the materialised image holds only CompareStringOrdinal, the PathCch
   family, lstrcpynA/W and lstrlenA.

   WHAT THIS MEASURES, and why each column is here:

     * NARROW vs WIDE on the SAME CHARACTER COUNT. That is the diagnostic that found 225. A narrow
       function doing half the bytes should be FASTER in wall-clock; when it is slower, the
       implementation differs rather than the workload.
     * BYTES PER NANOSECOND. 4-5 is a byte loop, ~22 is 16-byte SSE2, ~85 is 32-byte AVX2, ~150 is
       a 64-byte paired AVX2 loop. The number says which one is in there without disassembling.
     * SHORT AND LONG. A function can be fine at length and terrible at the call overhead, or the
       reverse, and only the ratio at both ends says which change is worth making.

   NOTHING HERE IS A CONTRACT. It is a shortlist. Every candidate that survives gets its own probe
   directory, because this repository has been burned by inheriting a sibling's rule: eight landed
   changes shipped wrong this session on one missing stopper, and StrStrA passed three weaker
   screens before dying on the fourth.                                                            */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint64_t sink;
static double _ns;
static LARGE_INTEGER qfreq;

#define TIME(iters, expr) do{                                            \
    LARGE_INTEGER _a,_b; double _best = 1e30;                            \
    for(int _t=0;_t<9;++_t){                                             \
        QueryPerformanceCounter(&_a);                                    \
        for(int _i=0;_i<(iters);++_i){ expr; }                           \
        QueryPerformanceCounter(&_b);                                    \
        double _d = (double)(_b.QuadPart-_a.QuadPart)*1e9/(double)qfreq.QuadPart/(iters); \
        if(_d<_best) _best=_d;                                           \
    }                                                                    \
    _ns=_best;                                                           \
}while(0)

static void bar2(const char* name, double a, double w){
    printf("  %-34s A %8.2f ns   W %8.2f ns   A/W %6.2fx\n", name, a, w, w>0 ? a/w : 0.0);
}
static void tput(const char* name, double ns, double bytes){
    printf("      %-30s %8.2f ns   %8.2f bytes/ns   %s\n", name, ns, bytes/ns,
           bytes/ns < 8   ? "<- a byte loop"          :
           bytes/ns < 40  ? "<- 16-byte SSE2"         :
           bytes/ns < 110 ? "<- 32-byte AVX2"         : "<- 64-byte paired AVX2");
}

static char  A4K[4200], A4K2[4200], ADST[8400];
static wchar_t W4K[4200], W4K2[4200], WDST[8400];
static char  A64[128], ADST64[256];
static wchar_t W64[128], WDST64[256];

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    QueryPerformanceFrequency(&qfreq);
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    for(int i=0;i<4000;i++){ A4K[i]=(char)('a'+i%23); W4K[i]=(wchar_t)(L'a'+i%23); }
    A4K[4000]=0; W4K[4000]=0;
    memcpy(A4K2, A4K, 4001);
    memcpy(W4K2, W4K, 4001*sizeof(wchar_t));
    for(int i=0;i<64;i++){ A64[i]=(char)('a'+i%23); W64[i]=(wchar_t)(L'a'+i%23); }
    A64[64]=0; W64[64]=0;

    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    HMODULE h32 = LoadLibraryW(L"kernel32.dll");
    printf("kernelbase = %p   kernel32 = %p\n", (void*)hk, (void*)h32);
    printf("GetACP() = %u\n\n", GetACP());

    #define RESOLVE(var, type, name) do{                                    \
        var = (type)GetProcAddress(hk, name);                               \
        if(!var && h32) var = (type)GetProcAddress(h32, name);              \
        if(!var) printf("  (%s not exported by name)\n", name);             \
    }while(0)

    typedef int      (WINAPI *T_LEN_A)(const char*);
    typedef int      (WINAPI *T_LEN_W)(const wchar_t*);
    typedef char*    (WINAPI *T_CPY_A)(char*, const char*);
    typedef wchar_t* (WINAPI *T_CPY_W)(wchar_t*, const wchar_t*);
    typedef char*    (WINAPI *T_CAT_A)(char*, const char*);
    typedef wchar_t* (WINAPI *T_CAT_W)(wchar_t*, const wchar_t*);

    T_LEN_A lena; T_LEN_W lenw;
    T_CPY_A cpya; T_CPY_W cpyw;
    T_CAT_A cata; T_CAT_W catw;
    RESOLVE(lena, T_LEN_A, "lstrlenA");
    RESOLVE(lenw, T_LEN_W, "lstrlenW");
    RESOLVE(cpya, T_CPY_A, "lstrcpyA");
    RESOLVE(cpyw, T_CPY_W, "lstrcpyW");
    RESOLVE(cata, T_CAT_A, "lstrcatA");
    RESOLVE(catw, T_CAT_W, "lstrcatW");

    printf("=== 1. lstrlen -- the BASELINE, already converted as change 225 ===\n");
    if (lena && lenw) {
        double a, w;
        TIME(200000, sink ^= (uint64_t)lena(A4K)); a = _ns;
        TIME(200000, sink ^= (uint64_t)lenw(W4K)); w = _ns;
        bar2("lstrlen 4000 chars", a, w);
        tput("lstrlenA 4000 bytes", a, 4000);
        tput("lstrlenW 4000 wchars", w, 8000);
        TIME(300000, sink ^= (uint64_t)lena(A64)); a = _ns;
        TIME(300000, sink ^= (uint64_t)lenw(W64)); w = _ns;
        bar2("lstrlen 64 chars", a, w);
    }

    printf("\n=== 2. lstrcpy -- a length scan plus a copy ===\n");
    if (cpya && cpyw) {
        double a, w;
        TIME(100000, sink ^= (uint64_t)(size_t)cpya(ADST, A4K)); a = _ns;
        TIME(100000, sink ^= (uint64_t)(size_t)cpyw(WDST, W4K)); w = _ns;
        bar2("lstrcpy 4000 chars", a, w);
        tput("lstrcpyA 4000 bytes", a, 4000);
        tput("lstrcpyW 4000 wchars", w, 8000);
        TIME(300000, sink ^= (uint64_t)(size_t)cpya(ADST64, A64)); a = _ns;
        TIME(300000, sink ^= (uint64_t)(size_t)cpyw(WDST64, W64)); w = _ns;
        bar2("lstrcpy 64 chars", a, w);
        /* and against the CRT, which is the honest ceiling for a copy */
        TIME(100000, sink ^= (uint64_t)(size_t)memcpy(ADST, A4K, 4001));
        tput("memcpy 4001 bytes (the ceiling)", _ns, 4000);
    }

    printf("\n=== 3. lstrcat -- a length scan of BOTH, then a copy ===\n");
    printf("  onto an EMPTY destination, so the destination scan is trivial and the cost is the\n");
    printf("  source scan plus the copy:\n");
    if (cata && catw) {
        double a, w;
        TIME(50000, (ADST[0]=0, sink ^= (uint64_t)(size_t)cata(ADST, A4K))); a = _ns;
        TIME(50000, (WDST[0]=0, sink ^= (uint64_t)(size_t)catw(WDST, W4K))); w = _ns;
        bar2("lstrcat 4000 onto empty", a, w);
        tput("lstrcatA 4000 bytes", a, 4000);
        tput("lstrcatW 4000 wchars", w, 8000);
        printf("  and onto a 4000-character destination, where the destination scan dominates:\n");
        TIME(20000, (memcpy(ADST, A4K, 4001), sink ^= (uint64_t)(size_t)cata(ADST, A64))); a = _ns;
        /* 4001 WCHARS, not 4002 bytes: copying 4002 bytes leaves WDST unterminated and the
           append then walks off the end of it. That is what this probe did on its first run,
           and it took the process down at exactly this line. */
        TIME(20000, (memcpy(WDST, W4K, 4001*sizeof(wchar_t)), sink ^= (uint64_t)(size_t)catw(WDST, W64))); w = _ns;
        bar2("lstrcat 64 onto 4000", a, w);
        TIME(300000, (ADST64[0]=0, sink ^= (uint64_t)(size_t)cata(ADST64, A64))); a = _ns;
        TIME(300000, (WDST64[0]=0, sink ^= (uint64_t)(size_t)catw(WDST64, W64))); w = _ns;
        bar2("lstrcat 64 onto empty", a, w);
    }

    printf("\n=== 4. what the shortlist should be ===\n");
    printf("  A candidate is worth a probe directory when it is BOTH slower per byte than a\n");
    printf("  32-byte AVX2 loop would be AND called often enough to matter. The bytes/ns column\n");
    printf("  above answers the first; the second is a judgement, and lstrcpy/lstrcat are about as\n");
    printf("  hot as the Win32 surface gets.\n");
    printf("\n  NOTE: all three of these swallow an access violation and return a value rather than\n");
    printf("  faulting (measured for lstrlenA in change 225's probe, and for lstrcpynA/W in 209 and\n");
    printf("  211). Any conversion needs the __try/__except wrapper, and what it RETURNS has to be\n");
    printf("  measured per function -- lstrlenA returns 0, lstrcpynA returns NULL.\n");

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
