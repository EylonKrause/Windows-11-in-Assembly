/* changes/292-filetimetosystemtime/reference.c
 *
 * ORACLE for kernel32!FileTimeToSystemTime. Deliberately naive and deliberately NOT the algorithm
 * impl.asm uses, so that agreement between the two is evidence rather than a tautology:
 *
 *   * the year is found by peeling 400-, 100-, 4- and 1-year blocks off the day count, exactly the
 *     way the textbook does it, with the two "==4" clamps that the last day of a century and the
 *     last day of a leap year need;
 *   * the month is found by walking a twelve-entry month table with an explicit leap-year test;
 *   * every division is a real C `/` and every remainder a real C `%`.
 *
 * impl.asm instead uses the era-based civil-from-days form with no month table, no leap-year branch
 * and no loop (change 126's engine). The two share no structure, so a mismatch in either direction
 * is a real defect and not a copied mistake.
 *
 * THE CONTRACT, as PROVED against the live export by probes/contract.c and probes/leapdata.c --
 * see RESULTS.md for the transcripts:
 *
 *   1. The FILETIME is read as one 64-bit value and tested SIGNED. t < 0  =>  the function fails.
 *   2. On failure it returns FALSE, sets the last error to ERROR_INVALID_PARAMETER (87), and does
 *      NOT write a single byte of *lpSystemTime. The validation happens before the store, proved by
 *      passing lpSystemTime = NULL together with a negative time and getting 0/87 instead of a
 *      fault.
 *   3. There is NO upper bound check. 0x7FFFFFFFFFFFFFFF is accepted and yields year 30828.
 *   4. On success the last error is NOT touched (a 0xDEADBEEF sentinel survives every call).
 *   5. wDayOfWeek IS filled in, as (days + 1) % 7 -- 1601-01-01 is a Monday and comes back as 1.
 *   6. Exactly 16 bytes of *lpSystemTime are written, in the SYSTEMTIME field order, which is a
 *      PERMUTATION of ntdll's TIME_FIELDS order: TIME_FIELDS puts Day at +4 and Weekday at +14,
 *      SYSTEMTIME puts wDayOfWeek at +4 and wDay at +6.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define TICKS_PER_DAY    864000000000LL
#define TICKS_PER_HOUR   36000000000LL
#define TICKS_PER_MIN    600000000LL
#define TICKS_PER_SEC    10000000LL
#define TICKS_PER_MS     10000LL

static int ref_is_leap(long long y)
{
    if (y % 4)   return 0;
    if (y % 100) return 1;
    return y % 400 == 0;
}

int ref_filetime_to_systemtime(const FILETIME* lpFileTime, SYSTEMTIME* lpSystemTime)
{
    long long t, days, rem, n400, n100, n4, n1, y, doy;
    static const int mlen[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int m, len;

    t = (long long)(((unsigned long long)lpFileTime->dwHighDateTime << 32) |
                     (unsigned long long)lpFileTime->dwLowDateTime);

    /* Contract point 1 and 2: signed test, and nothing is written when it fails. */
    if (t < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    days = t / TICKS_PER_DAY;
    rem  = t % TICKS_PER_DAY;

    /* Time of day -- four independent, obvious divisions. */
    lpSystemTime->wHour         = (WORD)(rem / TICKS_PER_HOUR);
    lpSystemTime->wMinute       = (WORD)((rem / TICKS_PER_MIN) % 60);
    lpSystemTime->wSecond       = (WORD)((rem / TICKS_PER_SEC) % 60);
    lpSystemTime->wMilliseconds = (WORD)((rem / TICKS_PER_MS)  % 1000);

    /* Contract point 5. 1601-01-01 was a Monday, and SYSTEMTIME numbers Sunday as 0. */
    lpSystemTime->wDayOfWeek = (WORD)((days + 1) % 7);

    /* Year, by peeling calendar blocks. 1601-01-01 is the start of a 400-year Gregorian cycle
       (1601..2000 holds exactly 97 leap years), so the decomposition is exact with no offset. */
    n400 = days / 146097; days %= 146097;      /* 400 years = 146097 days */
    n100 = days / 36524;                       /* 100 years =  36524 days */
    if (n100 == 4) n100 = 3;                   /* the last day of a 400-cycle lands one block high */
    days -= n100 * 36524;
    n4   = days / 1461;  days %= 1461;         /*   4 years =   1461 days */
    n1   = days / 365;                         /*   1 year  =    365 days */
    if (n1 == 4) n1 = 3;                       /* the last day of a leap year lands one block high */
    days -= n1 * 365;

    y   = 1601 + n400 * 400 + n100 * 100 + n4 * 4 + n1;
    doy = days;                                /* 0-based day of year */

    /* Month, by walking the table. */
    for (m = 0; m < 12; ++m) {
        len = mlen[m];
        if (m == 1 && ref_is_leap(y)) len = 29;
        if (doy < len) break;
        doy -= len;
    }

    lpSystemTime->wYear  = (WORD)y;
    lpSystemTime->wMonth = (WORD)(m + 1);
    lpSystemTime->wDay   = (WORD)(doy + 1);

    /* Contract point 4: the last error is deliberately left exactly as the caller had it. */
    return 1;
}
