// changes/293-systemtimetofiletime/reference.c
//
// The ORACLE for kernel32!SystemTimeToFileTime (a jmp thunk onto kernelbase!SystemTimeToFileTime).
// Deliberately naive scalar C. Written from what probes/contract.c and probes/leap_and_error.c
// PROVED against the live export on this machine, not from MSDN.
//
// WHAT THE LIVE EXPORT DOES (proved, see RESULTS.md):
//   * It copies seven of the eight SYSTEMTIME words into a stack TIME_FIELDS, forces Weekday to 0,
//     and calls ntdll!RtlTimeFieldsToTime.  wDayOfWeek (offset 4) is NEVER READ.
//   * The seven words are copied with `movzx`/16-bit stores, so they land in a CSHORT (signed
//     16-bit) field: a WORD above 0x7FFF becomes a negative TIME_FIELDS value and is rejected.
//   * Field ranges: Year 1601..30827 (30828 is a HARD BOUND, not an overflow -- 30828-01-01 would
//     still fit a positive int64), Month 1..12, Day 1..days-in-month with full Gregorian leap
//     rules, Hour 0..23, Minute 0..59, Second 0..59, Milliseconds 0..999.
//   * Second == 60 is REJECTED on this machine. PEB->LeapSecondData is present and its Enabled
//     byte is 1, but PEB->LeapSecondFlags bit 0 (SixtySecondEnabled) is 0 and the leap-second
//     record count is 0, so ntdll runs its leap-second-aware body and still produces exactly the
//     plain Gregorian answer -- verified over 874,248 cases including every second of the real
//     2016-12-31/2017-01-01 leap-second boundary.
//   * On success: returns 1, writes the 8-byte FILETIME, and does NOT touch TEB->LastErrorValue
//     or TEB->LastStatusValue.
//   * On failure: returns 0, LEAVES *lpFileTime UNTOUCHED, and sets LastStatusValue =
//     STATUS_INVALID_PARAMETER (0xC000000D) and LastErrorValue = ERROR_INVALID_PARAMETER (87).
//
// The date arithmetic here is the ERA-based days-from-civil of change 126/127. impl.asm uses a
// DIFFERENT closed form (365*yy + yy/4 - yy/100 + yy/400 with a per-month table), so the oracle and
// the implementation reach the same number by two independent derivations. correctness.c adds a
// third: a running day counter that is simply incremented one day at a time across the whole domain.

typedef struct {
    unsigned short wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} WIA_ST;

int wia_ref_is_leap(int y){ return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int wia_ref_days_in_month(int y, int m){
    static const int D[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && wia_ref_is_leap(y)) return 29;
    return D[m];
}

// Returns 1 and writes *out on success; returns 0 and leaves *out alone on failure.
int ref_systemtime_to_filetime(const WIA_ST* st, unsigned long long* out)
{
    int y  = (short)st->wYear;          // CSHORT semantics: 0x8000..0xFFFF are negative
    int mo = (short)st->wMonth;
    int d  = (short)st->wDay;
    int h  = (short)st->wHour;
    int mi = (short)st->wMinute;
    int se = (short)st->wSecond;
    int ms = (short)st->wMilliseconds;
    // st->wDayOfWeek is deliberately not read at all.

    if (mo < 1    || mo > 12)   return 0;
    if (d  < 1    || d  > wia_ref_days_in_month(y, mo)) return 0;
    if (y  < 1601 || y  > 30827) return 0;
    if (h  < 0    || h  > 23)   return 0;
    if (mi < 0    || mi > 59)   return 0;
    if (se < 0    || se > 59)   return 0;
    if (ms < 0    || ms > 999)  return 0;

    // days-from-civil, era based (year shifted to start in March)
    long long yy  = y - (mo <= 2);
    long long era = yy / 400;
    long long yoe = yy - era * 400;                              // 0..399
    long long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;        // 0..146096
    long long days = era * 146097 + doe - 584694;                 // days since 1601-01-01

    *out = (unsigned long long)( days * 864000000000LL
                               + (long long)h  * 36000000000LL
                               + (long long)mi * 600000000LL
                               + (long long)se * 10000000LL
                               + (long long)ms * 10000LL );
    return 1;
}
