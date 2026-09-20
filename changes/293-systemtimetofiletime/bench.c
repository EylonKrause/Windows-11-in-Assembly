// changes/293-systemtimetofiletime/bench.c
// Gate 2 for change 293: wia_systemtime_to_filetime vs the LIVE kernel32!SystemTimeToFileTime.
//
// A SYSTEMTIME is always 16 bytes, so there is no size axis here. The classes are instead the
// distinct PATHS through the function, which is what actually varies its cost:
//   * a plain date in a 31-day month, the fall-through path;
//   * the Unix epoch and the two ends of the legal domain, the extremes of the date arithmetic;
//   * February in a leap year and February in a non-leap year, the only path that runs the
//     leap-year test, and the one change 127 measured as its slowest;
//   * a date one day past the end of its month, the day-vs-month-length rejection;
//   * an out-of-range field; the rejection path, which also has to set the last error.
// A regression on ANY of them fails the gate.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

typedef struct { unsigned short wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } WIA_ST;
extern int wia_systemtime_to_filetime(const WIA_ST*, unsigned long long*);
typedef BOOL (WINAPI *fn)(const SYSTEMTIME*, LPFILETIME);
static fn sys;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ unsigned long long t=0; int r = wia_systemtime_to_filetime((const WIA_ST*)c,&t); return t ^ (uint64_t)r; }
static uint64_t op_sys (void* c){ FILETIME f; f.dwLowDateTime=0; f.dwHighDateTime=0;
    int r = sys((const SYSTEMTIME*)c,&f);
    return (((uint64_t)f.dwHighDateTime<<32)|f.dwLowDateTime) ^ (uint64_t)r; }
#pragma optimize("", on)

int main(void){
    sys = (fn)GetProcAddress(LoadLibraryW(L"kernel32.dll"),"SystemTimeToFileTime");
    if(!sys) return 2;
    enum { K = 8 };
    static WIA_ST F[K] = {
        {2023, 6,4,15,13,45, 7,123},    /* typical                      */
        {1601, 1,1, 1, 0, 0, 0,  0},    /* first instant of the domain  */
        {1970, 1,4, 1, 0, 0, 0,  0},    /* Unix epoch                   */
        {2024, 2,4,29,23,59,59,999},    /* leap day  - runs the leap test */
        {2023, 2,2,28,23,59,59,999},    /* February, not a leap year    */
        {30827,12,0,31,23,59,59,999},   /* last instant of the domain   */
        {2023, 6,4,31,13,45, 7,123},    /* day past the end of the month */
        {2023,13,4,15,13,45, 7,123}     /* invalid field - reject + SetLastError */
    };
    static const char* N[K] = { "typical","min 1601","epoch 1970","leap day","feb non-leap",
                                "max 30827","bad day","bad month" };
    static wia_case cs[K];
    for(int i=0;i<K;++i){ cs[i].label=N[i]; cs[i].bytes=16; cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=&F[i]; }
    return wia_bench_compare("kernel32 SystemTimeToFileTime (wia vs kernel32)", cs, K, 300);
}
