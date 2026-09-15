/* discovery/shlwapi_kernelbase.c
   Cost survey of shlwapi and kernelbase string/path exports this project has not covered, so the
   next targets are chosen by measurement rather than by guessing.

   Timed like the real benches: pinned core, raised priority, warm cache, minimum of several batches.
   A routine is only interesting here if it is BOTH slow per byte AND a leaf -- anything that reaches
   the registry, a locale table or another DLL is not a reimplementation candidate, however slow it
   looks.                                                                                           */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static LARGE_INTEGER F;
static volatile uint64_t sink;
static double _ns;

#define TIME(N, STMT) do {                                                     \
    LARGE_INTEGER _qa, _qb; double best = 1e300;                               \
    for (int i = 0; i < 2000; ++i) { STMT; }                                   \
    for (int t = 0; t < 7; ++t) {                                              \
        QueryPerformanceCounter(&_qa);                                         \
        for (int i = 0; i < (N); ++i) { STMT; }                                \
        QueryPerformanceCounter(&_qb);                                         \
        double ns = (double)(_qb.QuadPart - _qa.QuadPart) * 1e9                \
                    / (double)F.QuadPart / (double)(N);                        \
        if (ns < best) best = ns;                                              \
    }                                                                          \
    _ns = best;                                                                \
} while (0)

typedef int      (WINAPI *FN_W_W)(const wchar_t*, const wchar_t*);
typedef int      (WINAPI *FN_W_W_I)(const wchar_t*, const wchar_t*, int);
typedef wchar_t* (WINAPI *FN_STRW)(const wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *FN_CHRW)(const wchar_t*, wchar_t);
typedef int      (WINAPI *FN_TOINTW)(const wchar_t*);
typedef BOOL     (WINAPI *FN_MATCHW)(const wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *FN_LSTRCPYNW)(wchar_t*, const wchar_t*, int);
typedef int      (WINAPI *FN_LSTRLENW)(const wchar_t*);
typedef BOOL     (WINAPI *FN_PATHW)(const wchar_t*);
typedef int      (WINAPI *FN_CMPORD)(LPCWCH, int, LPCWCH, int, BOOL);

static wchar_t BIGA[4096], BIGB[4096], OUTB[4096];

static void bar(const char* name, double ns, double bytes){
    if (bytes > 0)
        printf("  %-34s %9.2f ns   %8.2f GB/s\n", name, ns, bytes / ns);
    else
        printf("  %-34s %9.2f ns\n", name, ns);
}

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");

    for (int i = 0; i < 4000; ++i) { BIGA[i] = (wchar_t)(L'a' + (i % 26)); BIGB[i] = BIGA[i]; }
    BIGA[4000] = BIGB[4000] = 0;

    printf("=== shlwapi: case-insensitive compares (1000 wide chars, equal) ===\n");
    {
        FN_W_W f = (FN_W_W)GetProcAddress(hs, "StrCmpIW");
        if (f) { TIME(200000, sink ^= (uint64_t)f(BIGA, BIGB)); bar("StrCmpIW  4000 wchar", _ns, 8000); }
        f = (FN_W_W)GetProcAddress(hs, "StrCmpW");
        if (f) { TIME(200000, sink ^= (uint64_t)f(BIGA, BIGB)); bar("StrCmpW   4000 wchar", _ns, 8000); }
        FN_W_W_I g = (FN_W_W_I)GetProcAddress(hs, "StrCmpNIW");
        if (g) { TIME(200000, sink ^= (uint64_t)g(BIGA, BIGB, 4000)); bar("StrCmpNIW 4000 wchar", _ns, 8000); }
        g = (FN_W_W_I)GetProcAddress(hs, "StrCmpNW");
        if (g) { TIME(200000, sink ^= (uint64_t)g(BIGA, BIGB, 4000)); bar("StrCmpNW  4000 wchar", _ns, 8000); }
    }
    {
        /* short strings too -- that is where a per-call setup cost shows */
        static const wchar_t A[] = L"Program Files", B[] = L"PROGRAM FILES";
        FN_W_W f = (FN_W_W)GetProcAddress(hs, "StrCmpIW");
        if (f) { TIME(500000, sink ^= (uint64_t)f(A, B)); bar("StrCmpIW  13 wchar", _ns, 26); }
        f = (FN_W_W)GetProcAddress(hk, "lstrcmpiW");
        if (f) { TIME(200000, sink ^= (uint64_t)f(A, B)); bar("kernelbase lstrcmpiW 13", _ns, 26); }
    }

    printf("\n=== shlwapi: search ===\n");
    {
        FN_STRW f = (FN_STRW)GetProcAddress(hs, "StrStrIW");
        static const wchar_t NEEDLE[] = L"xyzzy";
        if (f) { TIME(20000, sink ^= (uint64_t)(size_t)f(BIGA, NEEDLE)); bar("StrStrIW  4000 wchar miss", _ns, 8000); }
        f = (FN_STRW)GetProcAddress(hs, "StrStrW");
        if (f) { TIME(20000, sink ^= (uint64_t)(size_t)f(BIGA, NEEDLE)); bar("StrStrW   4000 wchar miss", _ns, 8000); }
        FN_CHRW c = (FN_CHRW)GetProcAddress(hs, "StrChrW");
        if (c) { TIME(50000, sink ^= (uint64_t)(size_t)c(BIGA, L'#')); bar("StrChrW   4000 wchar miss", _ns, 8000); }
        c = (FN_CHRW)GetProcAddress(hs, "StrChrIW");
        if (c) { TIME(50000, sink ^= (uint64_t)(size_t)c(BIGA, L'#')); bar("StrChrIW  4000 wchar miss", _ns, 8000); }
        c = (FN_CHRW)GetProcAddress(hs, "StrRChrW");
        if (c) { TIME(50000, sink ^= (uint64_t)(size_t)c(BIGA, L'#')); bar("StrRChrW  4000 wchar miss", _ns, 8000); }
    }

    printf("\n=== shlwapi: numbers and paths ===\n");
    {
        FN_TOINTW f = (FN_TOINTW)GetProcAddress(hs, "StrToIntW");
        static const wchar_t N[] = L"1234567890";
        if (f) { TIME(300000, sink ^= (uint64_t)f(N)); bar("StrToIntW 10 digits", _ns, 0); }
        FN_MATCHW m = (FN_MATCHW)GetProcAddress(hs, "PathMatchSpecW");
        static const wchar_t P[] = L"C:\\Windows\\System32\\kernelbase.dll", S[] = L"*.dll";
        if (m) { TIME(100000, sink ^= (uint64_t)m(P, S)); bar("PathMatchSpecW *.dll", _ns, 0); }
        FN_PATHW r = (FN_PATHW)GetProcAddress(hs, "PathIsRelativeW");
        if (r) { TIME(300000, sink ^= (uint64_t)r(P)); bar("PathIsRelativeW", _ns, 0); }
        r = (FN_PATHW)GetProcAddress(hs, "PathIsUNCW");
        if (r) { TIME(300000, sink ^= (uint64_t)r(P)); bar("PathIsUNCW", _ns, 0); }
        FN_CHRW e = (FN_CHRW)GetProcAddress(hs, "PathFindExtensionW");
        if (e) { TIME(300000, sink ^= (uint64_t)(size_t)((FN_TOINTW)e)(P)); bar("PathFindExtensionW", _ns, 0); }
    }

    printf("\n=== kernelbase: the lstr family ===\n");
    {
        FN_LSTRLENW l = (FN_LSTRLENW)GetProcAddress(hk, "lstrlenW");
        if (l) { TIME(100000, sink ^= (uint64_t)l(BIGA)); bar("lstrlenW  4000 wchar", _ns, 8000); }
        FN_LSTRCPYNW c = (FN_LSTRCPYNW)GetProcAddress(hk, "lstrcpynW");
        if (c) { TIME(100000, sink ^= (uint64_t)(size_t)c(OUTB, BIGA, 4000)); bar("lstrcpynW 4000 wchar", _ns, 8000); }
        if (c) { TIME(500000, sink ^= (uint64_t)(size_t)c(OUTB, BIGA, 16));   bar("lstrcpynW 16 wchar", _ns, 32); }
    }

    printf("\n=== kernelbase: CompareStringOrdinal (the modern ordinal compare) ===\n");
    {
        FN_CMPORD f = (FN_CMPORD)GetProcAddress(hk, "CompareStringOrdinal");
        if (f) {
            TIME(200000, sink ^= (uint64_t)f(BIGA, 4000, BIGB, 4000, FALSE));
            bar("CompareStringOrdinal 4000 cs", _ns, 8000);
            TIME(200000, sink ^= (uint64_t)f(BIGA, 4000, BIGB, 4000, TRUE));
            bar("CompareStringOrdinal 4000 ci", _ns, 8000);
            TIME(500000, sink ^= (uint64_t)f(BIGA, 13, BIGB, 13, TRUE));
            bar("CompareStringOrdinal 13 ci", _ns, 26);
        }
    }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
