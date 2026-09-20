/* changes/292-filetimetosystemtime/probes/refonly.c
 *
 * Throwaway. Step 4 of the procedure: prove reference.c against the LIVE export BEFORE a line of
 * assembly is written. A reference that disagrees with the live export means the contract is wrong,
 * and discovering that after writing impl.asm wastes the assembly.
 *
 * Sweep: every one of the 10 675 200 day boundaries in the domain (which is an EXHAUSTIVE proof of
 * the calendar half, because the date fields depend on nothing but the day count), the last tick of
 * every one of those days (the time-of-day corner), a mid-day offset on every day, the edges, and
 * 3M fixed-seed random instants. Compares all 8 fields, the BOOL, and the last error.
 *
 *   cl /nologo /O2 refonly.c ..\reference.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

int ref_filetime_to_systemtime(const FILETIME*, SYSTEMTIME*);
typedef BOOL (WINAPI *FT2ST)(const FILETIME*, LPSYSTEMTIME);
static FT2ST sys;
static int fails = 0;
static long long cases = 0;
#define TPD 864000000000LL
#define SENT 0xDEADBEEFu

static void chk(long long t)
{
    SYSTEMTIME a, b; FILETIME ft; BOOL ra, rb; DWORD ea, eb;
    memcpy(&ft, &t, 8);
    memset(&a, 0xCD, sizeof a); memset(&b, 0xCD, sizeof b);
    SetLastError(SENT); ra = sys(&ft, &a); ea = GetLastError();
    SetLastError(SENT); rb = ref_filetime_to_systemtime(&ft, &b); eb = GetLastError();
    ++cases;
    if (ra != rb || ea != eb || memcmp(&a, &b, sizeof a)) {
        if (fails < 12)
            printf("FAIL t=%lld  sys{r=%d e=%lu %u-%u-%u dow%u %u:%02u:%02u.%03u}  "
                   "ref{r=%d e=%lu %u-%u-%u dow%u %u:%02u:%02u.%03u}\n", t,
                   ra, ea, a.wYear, a.wMonth, a.wDay, a.wDayOfWeek, a.wHour, a.wMinute, a.wSecond, a.wMilliseconds,
                   rb, eb, b.wYear, b.wMonth, b.wDay, b.wDayOfWeek, b.wHour, b.wMinute, b.wSecond, b.wMilliseconds);
        ++fails;
    }
}

int main(void)
{
    long long d, i;
    unsigned long long s = 0x9E3779B97F4A7C15ULL;
    sys = (FT2ST)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FileTimeToSystemTime");
    if (!sys) { printf("no export\n"); return 2; }

    { static const long long E[] = {
        0, 1, 9999, 10000, TPD - 1, TPD, TPD * 365, TPD * 366, TPD * 145731,
        116444736000000000LL, 133200000000000000LL, 0x7FFFFFFFFFFFFFFFLL, 0x7FFFFFFFFFFFFFFELL,
        -1LL, (long long)0x8000000000000000ULL, -TPD, -10000000LL };
      for (i = 0; i < (long long)(sizeof E / sizeof E[0]); ++i) chk(E[i]); }

    for (d = 0; d < 10675200 && fails < 12; ++d) {
        chk(d * TPD);                  /* midnight            */
        chk(d * TPD + TPD - 1);        /* the last tick of it  */
        chk(d * TPD + 500110007LL);    /* an arbitrary interior instant */
    }
    for (i = 0; i < 3000000 && fails < 12; ++i) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        chk((long long)(s & 0x7FFFFFFFFFFFFFFFULL));
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        chk((long long)s);             /* half of these are negative: the reject path */
    }
    printf("%s  (%lld cases, %d fails)\n", fails ? "REFERENCE: FAIL" : "REFERENCE: MATCHES LIVE",
           cases, fails);
    return fails ? 1 : 0;
}
