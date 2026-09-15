/* Direct timing check for change 202, used to diagnose a 0.00 ns reading from the shared harness.
   Prints raw QPC ticks and a checksum so a removed loop is distinguishable from a fast one. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern DWORD wia_ConvertGuidToStringW(const GUID*, PWSTR, DWORD);
typedef DWORD (WINAPI *FN)(const GUID*, PWSTR, DWORD);

static wchar_t ob[256];
static volatile unsigned long long sink;
static volatile int counter;

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"iphlpapi.dll");
    FN sys = (FN)GetProcAddress(h, "ConvertGuidToStringW");
    GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};

    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    const int N = 1000000;
    printf("QPC freq = %lld\n", (long long)f.QuadPart);

    double bo = 1e300, bs = 1e300;
    long long to = 0, ts = 0;

    for (int t = 0; t < 7; ++t) {
        counter = 0;
        QueryPerformanceCounter(&a);
        for (int i = 0; i < N; ++i) { sink ^= wia_ConvertGuidToStringW(&g, ob, 64); counter++; }
        QueryPerformanceCounter(&b);
        long long d = b.QuadPart - a.QuadPart;
        if (t == 0) to = d;
        double ns = (double)d * 1e9 / (double)f.QuadPart / (double)N;
        if (ns < bo) bo = ns;

        QueryPerformanceCounter(&a);
        for (int i = 0; i < N; ++i) { sink ^= sys(&g, ob, 64); counter++; }
        QueryPerformanceCounter(&b);
        d = b.QuadPart - a.QuadPart;
        if (t == 0) ts = d;
        ns = (double)d * 1e9 / (double)f.QuadPart / (double)N;
        if (ns < bs) bs = ns;
    }

    printf("ours   : %8.3f ns   (first-trial ticks %lld)\n", bo, to);
    printf("system : %8.3f ns   (first-trial ticks %lld)\n", bs, ts);
    printf("ratio  : %.1fx     counter=%d (expect %d)  sink=%llu\n",
           bs / bo, counter, 2 * N, (unsigned long long)sink);
    wia_ConvertGuidToStringW(&g, ob, 64);
    printf("out=[%ls]\n", ob);
    return 0;
}
