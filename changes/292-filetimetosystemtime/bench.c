/* changes/292-filetimetosystemtime/bench.c
 *
 * GATE 2, against the LIVE kernel32!FileTimeToSystemTime.
 *
 * A FILETIME is always eight bytes, so there is no length axis and the "size classes" of this
 * benchmark are input classes instead -- the ones whose cost could plausibly differ: the epoch
 * itself, a date at each end of the useful range, the largest accepted value, and the reject path,
 * which is a separate code path in both implementations and therefore a separate row. The
 * arithmetic is branch-free and data-independent, so the accepted rows are expected to be flat;
 * a row that is not flat would mean a data-dependent path nobody intended.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern BOOL wia_filetime_to_systemtime(const FILETIME*, LPSYSTEMTIME);
typedef BOOL (WINAPI *FT2ST)(const FILETIME*, LPSYSTEMTIME);
static FT2ST sys;

#pragma optimize("", off)
static uint64_t op_ours(void* c)
{
    SYSTEMTIME st;
    BOOL r = wia_filetime_to_systemtime((const FILETIME*)c, &st);
    return (uint64_t)r ^ st.wYear ^ st.wDay ^ st.wDayOfWeek ^ st.wMilliseconds;
}
static uint64_t op_sys(void* c)
{
    SYSTEMTIME st;
    BOOL r = sys((const FILETIME*)c, &st);
    return (uint64_t)r ^ st.wYear ^ st.wDay ^ st.wDayOfWeek ^ st.wMilliseconds;
}
#pragma optimize("", on)

int main(void)
{
    static long long T[] = {
        0LL,                        /* 1601-01-01, the epoch                    */
        116444736000000000LL,       /* 1970-01-01                               */
        133200000000000000LL,       /* 2023                                     */
        0x7FFFFFFFFFFFFFFFLL,       /* year 30828 -- accepted, there is no cap  */
        -1LL                        /* the reject path: FALSE + ERROR_INVALID_PARAMETER */
    };
    static const char* N[] = { "1601 epoch", "1970", "2023", "max accepted", "negative(reject)" };
    enum { K = 5 };
    static wia_case cs[K];
    int i;
    sys = (FT2ST)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FileTimeToSystemTime");
    if (!sys) return 2;
    for (i = 0; i < K; ++i) {
        cs[i].label = N[i];
        cs[i].bytes = 8;
        cs[i].ours = op_ours;
        cs[i].system = op_sys;
        cs[i].ctx = &T[i];
    }
    return wia_bench_compare("kernel32 FileTimeToSystemTime (wia mulx civil-from-days vs kernelbase)",
                             cs, K, 300);
}
