/* discovery/narrow_and_path.c
   Cost survey of the narrow lstr family and the remaining shlwapi path helpers.

   Two questions. lstrcpynA is change 209's narrow sibling -- does it carry the same cost, and does
   it carry the same contract? (The A/W pairs in this project have gone both ways: 203 inherited 202's
   contract exactly, while 205's differed from ntdll's on the one detail that mattered.) And the path
   helpers that the first shlwapi sweep only touched lightly.

   Timed like the real benches: pinned core, raised priority, warm cache, minimum of several batches. */
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

typedef char*    (WINAPI *FN_CPNA)(char*, const char*, int);
typedef int      (WINAPI *FN_LENA)(const char*);
typedef int      (WINAPI *FN_CMPA)(const char*, const char*);
typedef wchar_t* (WINAPI *FN_PW)(const wchar_t*);
typedef BOOL     (WINAPI *FN_MATCH)(const wchar_t*, const wchar_t*);
typedef HRESULT  (WINAPI *FN_CCH)(wchar_t*, size_t);

static void bar(const char* n, double ns, double bytes){
    if (bytes > 0) printf("  %-36s %9.2f ns   %7.2f GB/s\n", n, ns, bytes/ns);
    else           printf("  %-36s %9.2f ns\n", n, ns);
}

static char  A8[16], A16[32], A64[128], A260[512], A4K[8192], OUTBUF[8192];
static wchar_t WP[512];

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");

    for (int i = 0; i < 4000; ++i) A4K[i] = (char)('a' + i % 26);
    A4K[4000] = 0;
    for (int i = 0; i < 8;   ++i) A8[i]   = (char)('a' + i % 26); A8[8] = 0;
    for (int i = 0; i < 16;  ++i) A16[i]  = (char)('a' + i % 26); A16[16] = 0;
    for (int i = 0; i < 64;  ++i) A64[i]  = (char)('a' + i % 26); A64[64] = 0;
    for (int i = 0; i < 260; ++i) A260[i] = (char)('a' + i % 26); A260[260] = 0;

    printf("=== kernelbase: the NARROW lstr family (209's siblings) ===\n");
    {
        FN_CPNA f = (FN_CPNA)GetProcAddress(hk, "lstrcpynA");
        if (f) {
            TIME(300000, sink ^= (uint64_t)(size_t)f(OUTBUF, A8, 16));    bar("lstrcpynA 8 chars",   _ns, 8);
            TIME(300000, sink ^= (uint64_t)(size_t)f(OUTBUF, A16, 32));   bar("lstrcpynA 16 chars",  _ns, 16);
            TIME(300000, sink ^= (uint64_t)(size_t)f(OUTBUF, A64, 128));  bar("lstrcpynA 64 chars",  _ns, 64);
            TIME(200000, sink ^= (uint64_t)(size_t)f(OUTBUF, A260, 300)); bar("lstrcpynA 260 chars", _ns, 260);
            TIME(50000,  sink ^= (uint64_t)(size_t)f(OUTBUF, A4K, 4096)); bar("lstrcpynA 4000 chars",_ns, 4000);
        } else printf("  lstrcpynA (not found)\n");
        FN_LENA l = (FN_LENA)GetProcAddress(hk, "lstrlenA");
        if (l) { TIME(200000, sink ^= (uint64_t)l(A4K)); bar("lstrlenA 4000", _ns, 4000); }
        FN_CMPA c = (FN_CMPA)GetProcAddress(hk, "lstrcmpA");
        if (c) { TIME(100000, sink ^= (uint64_t)c(A4K, A4K)); bar("lstrcmpA 4000 (equal)", _ns, 4000); }
        c = (FN_CMPA)GetProcAddress(hk, "lstrcmpiA");
        if (c) { TIME(100000, sink ^= (uint64_t)c(A4K, A4K)); bar("lstrcmpiA 4000 (equal)", _ns, 4000); }
    }

    printf("\n=== shlwapi / kernelbase: path helpers ===\n");
    {
        static const wchar_t P[] = L"C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe";
        FN_PW f = (FN_PW)GetProcAddress(hs, "PathFindExtensionW");
        if (f) { TIME(500000, sink ^= (uint64_t)(size_t)f(P)); bar("PathFindExtensionW (55 chars)", _ns, 0); }
        f = (FN_PW)GetProcAddress(hs, "PathFindFileNameW");
        if (f) { TIME(500000, sink ^= (uint64_t)(size_t)f(P)); bar("PathFindFileNameW (covered: 161)", _ns, 0); }
        FN_MATCH m = (FN_MATCH)GetProcAddress(hs, "PathMatchSpecW");
        if (m) {
            TIME(100000, sink ^= (uint64_t)m(P, L"*.exe"));  bar("PathMatchSpecW *.exe (hit)",  _ns, 0);
            TIME(100000, sink ^= (uint64_t)m(P, L"*.dll"));  bar("PathMatchSpecW *.dll (miss)", _ns, 0);
            TIME(100000, sink ^= (uint64_t)m(P, L"*a*b*c*")); bar("PathMatchSpecW *a*b*c*",     _ns, 0);
        }
        FN_CCH cch = (FN_CCH)GetProcAddress(hk, "PathCchRemoveFileSpec");
        if (cch) {
            TIME(200000, (memcpy(WP, P, sizeof(P)), sink ^= (uint64_t)cch(WP, 512)));
            bar("PathCchRemoveFileSpec", _ns, 0);
        }
        FN_PW s = (FN_PW)GetProcAddress(hs, "PathSkipRootW");
        if (s) { TIME(500000, sink ^= (uint64_t)(size_t)s(P)); bar("PathSkipRootW", _ns, 0); }
        FN_MATCH q = (FN_MATCH)GetProcAddress(hs, "PathIsPrefixW");
        if (q) { TIME(200000, sink ^= (uint64_t)q(L"C:\\Program Files", P)); bar("PathIsPrefixW", _ns, 0); }
    }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
