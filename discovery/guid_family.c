/* discovery/guid_family.c
   Cost survey of the remaining GUID-shaped routines plus a few leftover ntdll candidates, so the
   next targets are chosen by measurement rather than by guessing.

   Everything here is timed the same way as the real benches: pinned core, raised priority, warm
   cache, minimum of several batches. The point is only to rank candidates and to see which ones
   saturate (a routine whose cost does not grow with its input is doing fixed setup work, which is
   exactly what change 202 turned out to be).                                                    */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static LARGE_INTEGER F;
static volatile uint64_t sink;

#define TIME(N, STMT) do {                                                    \
    LARGE_INTEGER _qa, _qb; double best = 1e300;                                   \
    for (int i = 0; i < 2000; ++i) { STMT; }                                   \
    for (int t = 0; t < 7; ++t) {                                              \
        QueryPerformanceCounter(&_qa);                                           \
        for (int i = 0; i < (N); ++i) { STMT; }                                \
        QueryPerformanceCounter(&_qb);                                           \
        double ns = (double)(_qb.QuadPart - _qa.QuadPart) * 1e9                    \
                    / (double)F.QuadPart / (double)(N);                        \
        if (ns < best) best = ns;                                              \
    }                                                                          \
    _ns = best;                                                                \
} while (0)

static double _ns;

typedef HRESULT (WINAPI *FN_CLSIDFromString)(const wchar_t*, GUID*);
typedef HRESULT (WINAPI *FN_IIDFromString)(const wchar_t*, GUID*);
typedef int     (WINAPI *FN_StringFromGUID2)(const GUID*, wchar_t*, int);
typedef HRESULT (WINAPI *FN_StringFromCLSID)(const GUID*, wchar_t**);
typedef LONG    (WINAPI *FN_UuidFromStringW)(unsigned short*, GUID*);
typedef LONG    (WINAPI *FN_UuidFromStringA)(unsigned char*, GUID*);
typedef LONG    (WINAPI *FN_UuidToStringW)(const GUID*, unsigned short**);
typedef LONG    (WINAPI *FN_UuidCompare)(GUID*, GUID*, LONG*);
typedef LONG    (WINAPI *FN_UuidEqual)(GUID*, GUID*, LONG*);
typedef LONG    (WINAPI *FN_UuidIsNil)(GUID*, LONG*);

typedef BOOLEAN (NTAPI *FN_RtlIsZeroMemory)(const void*, SIZE_T);
typedef ULONG   (NTAPI *FN_RtlFindClearRuns)(const void* bm, void* runs, ULONG n, BOOLEAN sorted);
typedef ULONG64 (NTAPI *FN_RtlUdiv128)(ULONG64 hi, ULONG64 lo, ULONG64 d, ULONG64* rem);
typedef ULONG   (NTAPI *FN_RtlNumberOfSetBits)(const void*);

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef struct { ULONG start, len; } RUN;

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    HMODULE hc = LoadLibraryW(L"combase.dll");
    HMODULE ho = LoadLibraryW(L"ole32.dll");
    HMODULE hr = LoadLibraryW(L"rpcrt4.dll");
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");

    static const GUID G = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
    static const wchar_t SB[] = L"{DEADBEEF-1234-5678-9ABC-DEF011223344}";
    static const wchar_t SN[] = L"DEADBEEF-1234-5678-9ABC-DEF011223344";
    static const char  SNA[] =  "DEADBEEF-1234-5678-9ABC-DEF011223344";
    static wchar_t wout[64];
    static GUID g2;

    printf("=== GUID formatting / parsing (ns per call) ===\n");

    {
        FN_StringFromGUID2 f = (FN_StringFromGUID2)GetProcAddress(hc, "StringFromGUID2");
        if (!f) f = (FN_StringFromGUID2)GetProcAddress(ho, "StringFromGUID2");
        if (f) { TIME(300000, sink ^= (uint64_t)f(&G, wout, 64));
                 printf("  %-34s %8.2f   -> \"%ls\"\n", "StringFromGUID2", _ns, wout); }
        else printf("  StringFromGUID2                    (not found)\n");
    }
    {
        FN_CLSIDFromString f = (FN_CLSIDFromString)GetProcAddress(hc, "CLSIDFromString");
        if (!f) f = (FN_CLSIDFromString)GetProcAddress(ho, "CLSIDFromString");
        if (f) { TIME(200000, sink ^= (uint64_t)f(SB, &g2));
                 printf("  %-34s %8.2f   (hr=%08lX, Data1=%08lX)\n", "CLSIDFromString {braced}",
                        _ns, (unsigned long)f(SB,&g2), (unsigned long)g2.Data1); }
        else printf("  CLSIDFromString                    (not found)\n");
    }
    {
        FN_IIDFromString f = (FN_IIDFromString)GetProcAddress(hc, "IIDFromString");
        if (!f) f = (FN_IIDFromString)GetProcAddress(ho, "IIDFromString");
        if (f) { TIME(200000, sink ^= (uint64_t)f(SB, &g2));
                 printf("  %-34s %8.2f\n", "IIDFromString {braced}", _ns); }
        else printf("  IIDFromString                      (not found)\n");
    }
    {
        FN_UuidFromStringW f = (FN_UuidFromStringW)GetProcAddress(hr, "UuidFromStringW");
        if (f) { TIME(200000, sink ^= (uint64_t)f((unsigned short*)SN, &g2));
                 printf("  %-34s %8.2f   (Data1=%08lX)\n", "rpcrt4!UuidFromStringW", _ns,
                        (unsigned long)g2.Data1); }
    }
    {
        FN_UuidFromStringA f = (FN_UuidFromStringA)GetProcAddress(hr, "UuidFromStringA");
        if (f) { TIME(200000, sink ^= (uint64_t)f((unsigned char*)SNA, &g2));
                 printf("  %-34s %8.2f\n", "rpcrt4!UuidFromStringA", _ns); }
    }
    {
        FN_UuidCompare f = (FN_UuidCompare)GetProcAddress(hr, "UuidCompare");
        GUID a = G, b = G; LONG st;
        if (f) { TIME(500000, sink ^= (uint64_t)f(&a, &b, &st));
                 printf("  %-34s %8.2f\n", "rpcrt4!UuidCompare (equal)", _ns); }
    }
    {
        FN_UuidEqual f = (FN_UuidEqual)GetProcAddress(hr, "UuidEqual");
        GUID a = G, b = G; LONG st;
        if (f) { TIME(500000, sink ^= (uint64_t)f(&a, &b, &st));
                 printf("  %-34s %8.2f\n", "rpcrt4!UuidEqual (equal)", _ns); }
    }
    {
        FN_UuidIsNil f = (FN_UuidIsNil)GetProcAddress(hr, "UuidIsNil");
        GUID a = G; LONG st;
        if (f) { TIME(500000, sink ^= (uint64_t)f(&a, &st));
                 printf("  %-34s %8.2f\n", "rpcrt4!UuidIsNil", _ns); }
    }

    printf("\n=== leftover ntdll candidates ===\n");
    {
        FN_RtlIsZeroMemory f = (FN_RtlIsZeroMemory)GetProcAddress(hn, "RtlIsZeroMemory");
        if (f) {
            static unsigned char buf[1 << 20];
            const int L[] = { 8, 32, 128, 1024, 8192, 65536, 1<<20 };
            const char* N[] = { "8 B","32 B","128 B","1 KB","8 KB","64 KB","1 MB" };
            printf("  RtlIsZeroMemory (all zero):\n");
            for (int k = 0; k < 7; ++k) {
                int n = L[k];
                TIME(n >= 65536 ? 20000 : 300000, sink ^= (uint64_t)f(buf, n));
                printf("    %-8s %8.2f ns   %7.2f GB/s\n", N[k], _ns, n / _ns);
            }
        } else printf("  RtlIsZeroMemory                    (not exported)\n");
    }
    {
        FN_RtlUdiv128 f = (FN_RtlUdiv128)GetProcAddress(hn, "RtlUdiv128");
        if (f) {
            ULONG64 rem;
            TIME(300000, sink ^= f(0x1234ull, 0xDEADBEEFCAFEBABEull, 0x9E3779B97F4A7C15ull, &rem));
            printf("  %-34s %8.2f\n", "RtlUdiv128", _ns);
        } else printf("  RtlUdiv128                        (not exported)\n");
    }
    {
        FN_RtlFindClearRuns f = (FN_RtlFindClearRuns)GetProcAddress(hn, "RtlFindClearRuns");
        if (f) {
            static ULONG words[2048];
            static RUN runs[64];
            RBM bm; bm.SizeOfBitMap = 2048 * 32; bm.Buffer = words;
            for (int i = 0; i < 2048; ++i) words[i] = (i % 3) ? 0xFFFFFFFFu : 0;
            TIME(100000, sink ^= (uint64_t)f(&bm, runs, 16, TRUE));
            printf("  %-34s %8.2f   (64Kb bitmap, 16 runs, sorted)\n", "RtlFindClearRuns", _ns);
            TIME(100000, sink ^= (uint64_t)f(&bm, runs, 16, FALSE));
            printf("  %-34s %8.2f   (unsorted)\n", "RtlFindClearRuns", _ns);
        } else printf("  RtlFindClearRuns                  (not exported)\n");
    }
    {
        FN_RtlNumberOfSetBits f = (FN_RtlNumberOfSetBits)GetProcAddress(hn, "RtlNumberOfSetBits");
        if (f) {
            static ULONG words[2048];
            RBM bm; bm.SizeOfBitMap = 2048 * 32; bm.Buffer = words;
            for (int i = 0; i < 2048; ++i) words[i] = 0xA5A5A5A5u;
            TIME(100000, sink ^= (uint64_t)f(&bm));
            printf("  %-34s %8.2f   (64Kb bitmap -> %.2f GB/s)\n", "RtlNumberOfSetBits", _ns,
                   8192.0 / _ns);
        } else printf("  RtlNumberOfSetBits                (not exported)\n");
    }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
