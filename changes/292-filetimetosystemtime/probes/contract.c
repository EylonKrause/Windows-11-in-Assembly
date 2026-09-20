/* changes/292-filetimetosystemtime/probes/contract.c
 *
 * Throwaway contract probe. Nothing here is a gate, it exists to PIN what the live export
 * actually does before reference.c is written, because change 289 proved MSDN wrong three times
 * this week. Every question below is answered by observation, not by documentation.
 *
 *   Q1  what does a NEGATIVE FILETIME do?  return value, last error, is *lpSystemTime touched?
 *   Q2  is the last error touched on SUCCESS?  (sentinel written before every call)
 *   Q3  is wDayOfWeek filled in, and with what?
 *   Q4  FILETIME == 0, and FILETIME == 0x7FFFFFFFFFFFFFFF (year > 30827), accepted or rejected?
 *   Q5  is kernel32's export the same code as kernelbase's?
 *   Q6  is the answer identical to ntdll!RtlTimeToTimeFields, permuted?  (change 126 is that engine)
 *   Q7  is leap-second support live on this machine?  PEB.LeapSecondData / LeapSecondFlags decide
 *       which body RtlpTimeToTimeFields runs, so read them rather than assume.
 *   Q8  NULL arguments, fault, or handled?
 *
 * cl /nologo /O2 contract.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>

typedef BOOL (WINAPI *FT2ST)(const FILETIME*, LPSYSTEMTIME);
typedef struct { short Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; } TF;
typedef void (WINAPI *T2TF)(const LONGLONG*, TF*);

static FT2ST k32, kbase;
static T2TF  rtl;

#define SENTINEL 0xDEADBEEFu

static void shot(const char* name, long long t)
{
    SYSTEMTIME st;
    FILETIME ft;
    BOOL r;
    DWORD le;
    unsigned char* p = (unsigned char*)&st;
    int i, touched = 0;

    memset(&st, 0xCD, sizeof st);
    memcpy(&ft, &t, 8);
    SetLastError(SENTINEL);
    r = k32(&ft, &st);
    le = GetLastError();
    for (i = 0; i < (int)sizeof st; ++i) if (p[i] != 0xCD) { touched = 1; break; }

    printf("%-26s t=%-22lld ret=%d  lasterr=%-10lu buf%s", name, t, r, le,
           touched ? "=" : " UNTOUCHED\n");
    if (touched)
        printf("{y=%u m=%u dow=%u d=%u %02u:%02u:%02u.%03u}\n",
               st.wYear, st.wMonth, st.wDayOfWeek, st.wDay,
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

static void vs_rtl(long long t)
{
    SYSTEMTIME st; FILETIME ft; TF tf;
    int ok;
    memcpy(&ft, &t, 8);
    memset(&st, 0, sizeof st); memset(&tf, 0, sizeof tf);
    k32(&ft, &st);
    rtl(&t, &tf);
    ok = st.wYear == (WORD)tf.Year && st.wMonth == (WORD)tf.Month &&
         st.wDay == (WORD)tf.Day && st.wDayOfWeek == (WORD)tf.Weekday &&
         st.wHour == (WORD)tf.Hour && st.wMinute == (WORD)tf.Minute &&
         st.wSecond == (WORD)tf.Second && st.wMilliseconds == (WORD)tf.Milliseconds;
    printf("  t=%-22lld st{%u-%u-%u dow%u %u:%02u:%02u.%03u}  TIME_FIELDS%s\n",
           t, st.wYear, st.wMonth, st.wDay, st.wDayOfWeek,
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
           ok ? " == permuted (Year,Month,Weekday->dow,Day,Hour,Minute,Second,Ms)" : " *** DIFFERS ***");
}

int main(void)
{
    HMODULE h32 = LoadLibraryW(L"kernel32.dll");
    HMODULE hkb = LoadLibraryW(L"kernelbase.dll");
    HMODULE hnt = LoadLibraryW(L"ntdll.dll");
    long long i;
    unsigned char* peb;

    k32   = (FT2ST)GetProcAddress(h32, "FileTimeToSystemTime");
    kbase = (FT2ST)GetProcAddress(hkb, "FileTimeToSystemTime");
    rtl   = (T2TF) GetProcAddress(hnt, "RtlTimeToTimeFields");
    printf("kernel32!FileTimeToSystemTime   = %p\n", (void*)k32);
    printf("kernelbase!FileTimeToSystemTime = %p   (Q5: %s)\n", (void*)kbase,
           (void*)k32 == (void*)kbase ? "SAME code" : "different addresses (k32 is a jmp thunk)");
    printf("first 6 bytes of kernel32's:");
    for (i = 0; i < 6; ++i) printf(" %02X", ((unsigned char*)k32)[i]);
    printf("\n\n");

    /* Q7, PEB.LeapSecondData (+0x7B8) and PEB.LeapSecondFlags (+0x7C0) on this build. */
    peb = (unsigned char*)__readgsqword(0x60);   /* gs:[0x60] IS the PEB pointer */
    printf("Q7 leap seconds: PEB=%p  LeapSecondData=%p  LeapSecondFlags=%08X\n",
           (void*)peb, *(void**)(peb + 0x7B8), *(unsigned*)(peb + 0x7C0));
    {
        void* lsd = *(void**)(peb + 0x7B8);
        printf("                 -> %s\n",
               (!lsd || *(unsigned char*)lsd == 0)
                 ? "DISABLED: RtlpTimeToTimeFields takes the plain body (== RtlTimeToTimeFields)"
                 : "ENABLED -- the leap-second body runs, contract must be re-derived");
    }
    printf("\n");

    printf("Q1/Q2/Q3/Q4 -- return, last error (sentinel %08X written before each call), buffer:\n", SENTINEL);
    shot("zero",                 0LL);
    shot("1 tick",               1LL);
    shot("1970 epoch",           116444736000000000LL);
    shot("2023-ish",             133200000000000000LL);
    shot("MAX positive",         0x7FFFFFFFFFFFFFFFLL);
    shot("MAX positive - 1",     0x7FFFFFFFFFFFFFFELL);
    shot("year 30827 boundary",  9223372036854775807LL - 100000000LL);
    shot("negative: -1",        -1LL);
    shot("negative: min",        (long long)0x8000000000000000ULL);
    shot("negative: -864000000000", -864000000000LL);
    shot("negative: -10000000",  -10000000LL);
    printf("\n");

    printf("Q6 -- against ntdll!RtlTimeToTimeFields (the engine change 126 already reproduces):\n");
    vs_rtl(0LL);
    vs_rtl(116444736000000000LL);
    vs_rtl(133200000000000000LL);
    vs_rtl(0x7FFFFFFFFFFFFFFFLL);
    vs_rtl(864000000000LL * 145731LL);
    printf("\n");

    /* Q3 continued: weekday over seven consecutive days from 1601-01-01. */
    printf("Q3 -- wDayOfWeek over the first 8 days (1601-01-01 was a Monday => 1):\n   ");
    for (i = 0; i < 8; ++i) {
        SYSTEMTIME st; FILETIME ft; long long t = i * 864000000000LL;
        memcpy(&ft, &t, 8); k32(&ft, &st);
        printf(" %u-%u-%u:dow%u", st.wYear, st.wMonth, st.wDay, st.wDayOfWeek);
    }
    printf("\n\n");

    /* Q8, NULL arguments. */
    printf("Q8 -- NULL arguments:\n");
    __try {
        SYSTEMTIME st; BOOL r;
        SetLastError(SENTINEL);
        r = k32(NULL, &st);
        printf("   lpFileTime=NULL     -> ret=%d lasterr=%lu (NO fault)\n", r, GetLastError());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("   lpFileTime=NULL     -> ACCESS VIOLATION (0x%08lX)\n", GetExceptionCode());
    }
    __try {
        FILETIME ft; BOOL r; long long t = 133200000000000000LL;
        memcpy(&ft, &t, 8);
        SetLastError(SENTINEL);
        r = k32(&ft, NULL);
        printf("   lpSystemTime=NULL   -> ret=%d lasterr=%lu (NO fault)\n", r, GetLastError());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("   lpSystemTime=NULL   -> ACCESS VIOLATION (0x%08lX)\n", GetExceptionCode());
    }
    __try {
        FILETIME ft; BOOL r; long long t = -1LL;
        memcpy(&ft, &t, 8);
        SetLastError(SENTINEL);
        r = k32(&ft, NULL);
        printf("   negative + NULL out -> ret=%d lasterr=%lu (validated BEFORE the write)\n",
               r, GetLastError());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("   negative + NULL out -> ACCESS VIOLATION (0x%08lX)\n", GetExceptionCode());
    }
    return 0;
}
