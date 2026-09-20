/* changes/292-filetimetosystemtime/correctness.c
 *
 * Gate 1. wia_filetime_to_systemtime vs reference.c (the naive oracle) and vs the live
 * kernel32!FileTimeToSystemTime resolved with GetProcAddress. Every case compares, for all three:
 *
 *      the BOOL return, the last error (a 0xDEADBEEF sentinel is written before every call, so
 *      "did not touch it" is as observable as "set it to 87"), all sixteen bytes of the SYSTEMTIME,
 *      and 32 canary bytes on each side of it.
 *
 * A single mismatch fails the gate.
 *
 * The corpus, and how the repository's minimum maps onto a function whose input is a fixed eight
 * bytes. There is no length axis here, so "every length up to twice the vector width" and "the
 * match at every position" are answered by exhausting the two axes the value actually has:
 *
 *   a. The calendar axis, exhaustive. The date fields depend on nothing but the day count, so
 *      every one of the 10 675 200 day boundaries in the domain is tested -- at midnight, at the
 *      last tick of the day, and at an interior instant. That is not a sample of the calendar, it
 *      is all of it.
 *
 *   B. The time-of-day axis, exhaustive. wHour, wMinute, wSecond and wMilliseconds are all
 *      functions of floor(rem / 10000), because 10000 divides each of 10^7, 6*10^8 and 3.6*10^10.
 *      floor(n/d) changes only at multiples of d, so testing rem = k*10000 and rem = k*10000 - 1
 *      for every k in [0, 86 400 000) hits every quotient boundary of every one of the four
 *      divisors. That is a proof of the four magic numbers over their whole operand range, not a
 *      sample -- the same standard change 126 held its constants to.
 *
 *   C. Edges: 0; 1 tick; 9999 and 10000 ticks; the last tick of day 0; the 1970 epoch;
 *      0x7FFFFFFFFFFFFFFF and 0x7FFFFFFFFFFFFFFE (year 30828, ACCEPTED -- there is no upper bound);
 *      -1; 0x8000000000000000; and negative day and second multiples.
 *
 *   D. Alignment: the FILETIME placed at all 8 byte offsets of a qword, the SYSTEMTIME at all 16.
 *
 *   E. Page safety: a filetime occupying the last 8 Bytes of a committed page with the next page
 *      PAGE_NOACCESS, and a SYSTEMTIME occupying the last 16 bytes of a committed page with the
 *      next page PAGE_NOACCESS. Both at once, too. A wide load or a wide store would fault.
 *
 *   F. Nothing written past the logical end, and nothing written AT ALL on the reject path: the
 *      SYSTEMTIME is embedded in a 0xA5 canary field and the whole field is compared afterwards;
 *      on a negative time the sixteen bytes must still read 0xCD from the pre-fill.
 *
 *   G. A large randomized fuzz set with a FIXED seed: 3 000 000 positive instants at full
 *      resolution and 3 000 000 unrestricted 64-bit values, half of which take the reject path.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern BOOL wia_filetime_to_systemtime(const FILETIME*, LPSYSTEMTIME);
int ref_filetime_to_systemtime(const FILETIME*, SYSTEMTIME*);

typedef BOOL (WINAPI *FT2ST)(const FILETIME*, LPSYSTEMTIME);
static FT2ST sys;

static int          fails = 0;
static long long    cases = 0;

#define TPD   864000000000LL
#define SENT  0xDEADBEEFu
#define GUARD 32

static void show(const char* who, BOOL r, DWORD e, const SYSTEMTIME* s)
{
    printf("   %-6s ret=%d err=%-10lu %u-%02u-%02u dow%u %02u:%02u:%02u.%03u\n",
           who, r, e, s->wYear, s->wMonth, s->wDay, s->wDayOfWeek,
           s->wHour, s->wMinute, s->wSecond, s->wMilliseconds);
}

/* One comparison. ft and st point at caller-chosen storage so the same routine serves the aligned,
   the unaligned and the page-boundary corpora. */
static void chk_at(long long t, FILETIME* ft, SYSTEMTIME* a, SYSTEMTIME* b, SYSTEMTIME* c,
                   const char* where)
{
    BOOL ra, rb, rc; DWORD ea, eb, ec;
    memcpy(ft, &t, 8);
    memset(a, 0xCD, sizeof(SYSTEMTIME));
    memset(b, 0xCD, sizeof(SYSTEMTIME));
    memset(c, 0xCD, sizeof(SYSTEMTIME));
    SetLastError(SENT); ra = sys(ft, a);                        ea = GetLastError();
    SetLastError(SENT); rb = wia_filetime_to_systemtime(ft, b); eb = GetLastError();
    SetLastError(SENT); rc = (BOOL)ref_filetime_to_systemtime(ft, c); ec = GetLastError();
    ++cases;
    if (ra != rb || ra != rc || ea != eb || ea != ec ||
        memcmp(a, b, sizeof(SYSTEMTIME)) || memcmp(a, c, sizeof(SYSTEMTIME))) {
        if (fails < 12) {
            printf("FAIL t=%lld (%s)\n", t, where);
            show("live", ra, ea, a); show("ours", rb, eb, b); show("ref", rc, ec, c);
        }
        ++fails;
    }
}

/* The default storage: a SYSTEMTIME wrapped in canaries, so "wrote past the end" is caught. */
static void chk(long long t)
{
    static unsigned char pa[GUARD * 2 + 16], pb[GUARD * 2 + 16], pc[GUARD * 2 + 16];
    FILETIME ft;
    memset(pa, 0xA5, sizeof pa); memset(pb, 0xA5, sizeof pb); memset(pc, 0xA5, sizeof pc);
    chk_at(t, &ft, (SYSTEMTIME*)(pa + GUARD), (SYSTEMTIME*)(pb + GUARD), (SYSTEMTIME*)(pc + GUARD),
           "canaried");
    {
        int i;
        for (i = 0; i < GUARD * 2 + 16; ++i) {
            if (i >= GUARD && i < GUARD + 16) continue;
            if (pa[i] != 0xA5 || pb[i] != 0xA5 || pc[i] != 0xA5) {
                if (fails < 12) printf("FAIL t=%lld canary clobbered at %+d\n", t, i - GUARD);
                ++fails; return;
            }
        }
    }
    /* On the reject path the 0xCD pre-fill must survive untouched in all three. */
    if (t < 0) {
        int i;
        for (i = 0; i < 16; ++i)
            if (pa[GUARD + i] != 0xCD || pb[GUARD + i] != 0xCD || pc[GUARD + i] != 0xCD) {
                if (fails < 12) printf("FAIL t=%lld wrote the output on the reject path\n", t);
                ++fails; return;
            }
    }
}

int main(void)
{
    long long d, k, i;
    unsigned long long s = 0x9E3779B97F4A7C15ULL;
    SYSTEM_INFO si;
    unsigned char *ipage, *opage;
    DWORD old, pg;

    sys = (FT2ST)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FileTimeToSystemTime");
    if (!sys) { printf("no kernel32!FileTimeToSystemTime\n"); return 2; }

    /* ---- C. edges --------------------------------------------------------------------------- */
    {
        static const long long E[] = {
            0, 1, 9999, 10000, TPD - 1, TPD, TPD + 1, TPD * 31, TPD * 365, TPD * 366,
            TPD * 145731,                      /* 2000-01-01, a 400-year cycle boundary        */
            TPD * 36524, TPD * 36525,          /* 1700-03-01 region: a skipped leap year       */
            116444736000000000LL,              /* 1970-01-01                                    */
            133200000000000000LL,
            0x7FFFFFFFFFFFFFFFLL, 0x7FFFFFFFFFFFFFFELL, 0x7FFFFFFFFFFFFFFFLL - TPD,
            -1LL, (long long)0x8000000000000000ULL, (long long)0x8000000000000001ULL,
            -TPD, -10000000LL, -10000LL, -9999LL };
        for (i = 0; i < (long long)(sizeof E / sizeof E[0]); ++i) chk(E[i]);
    }

    /* ---- D. every byte offset for the input and for the output ------------------------------ */
    {
        static unsigned char ibuf[24], ob1[48], ob2[48], ob3[48];
        int io, oo;
        static const long long T[] = { 0, TPD - 1, 133200000000000000LL, 0x7FFFFFFFFFFFFFFFLL, -1LL };
        for (io = 0; io < 8; ++io)
            for (oo = 0; oo < 16; ++oo)
                for (i = 0; i < 5; ++i)
                    chk_at(T[i], (FILETIME*)(ibuf + io),
                           (SYSTEMTIME*)(ob1 + oo), (SYSTEMTIME*)(ob2 + oo),
                           (SYSTEMTIME*)(ob3 + oo), "unaligned");
    }

    /* ---- E. page boundaries: the buffer ends exactly at a page, next page PAGE_NOACCESS ------ */
    GetSystemInfo(&si);
    pg = si.dwPageSize;
    ipage = (unsigned char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
    opage = (unsigned char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!ipage || !opage) { printf("VirtualAlloc failed\n"); return 2; }
    /* commit only the FIRST page of each reservation: the second stays reserved-but-not-committed,
       which is PAGE_NOACCESS as hard as it gets -- any touch of it is an access violation. */
    if (!VirtualAlloc(ipage, pg, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(opage, pg, MEM_COMMIT, PAGE_READWRITE)) { printf("commit failed\n"); return 2; }
    (void)old;
    {
        static unsigned char ob2[48], ob3[48];
        static const long long T[] = { 0, 1, TPD - 1, TPD, 116444736000000000LL,
                                       133200000000000000LL, 0x7FFFFFFFFFFFFFFFLL,
                                       -1LL, (long long)0x8000000000000000ULL };
        for (i = 0; i < (long long)(sizeof T / sizeof T[0]); ++i) {
            /* input's last byte is the page's last byte */
            chk_at(T[i], (FILETIME*)(ipage + pg - 8),
                   (SYSTEMTIME*)ob2, (SYSTEMTIME*)(ob2 + 16), (SYSTEMTIME*)ob3, "input at page end");
            /* output's last byte is the page's last byte */
            chk_at(T[i], (FILETIME*)ipage,
                   (SYSTEMTIME*)(opage + pg - 16), (SYSTEMTIME*)ob2, (SYSTEMTIME*)ob3,
                   "output at page end");
            /* both at once */
            chk_at(T[i], (FILETIME*)(ipage + pg - 8),
                   (SYSTEMTIME*)(opage + pg - 16), (SYSTEMTIME*)ob2, (SYSTEMTIME*)ob3,
                   "both at page end");
        }
    }

    /* ---- A. EXHAUSTIVE over the calendar: every day boundary in the whole domain ------------- */
    for (d = 0; d < 10675200 && fails < 12; ++d) {
        chk(d * TPD);                                   /* 00:00:00.000        */
        chk(d * TPD + TPD - 1);                         /* 23:59:59.999 + 9999 */
        chk(d * TPD + 500110007LL);                     /* an interior instant */
    }

    /* ---- B. EXHAUSTIVE over the time of day: every quotient boundary of every divisor -------- */
    {
        static unsigned char pa[16], pb[16], pc[16];
        const long long base = 145731LL * TPD;          /* 2000-01-01, an ordinary day */
        FILETIME ft;
        for (k = 0; k < 86400000 && fails < 12; ++k) {
            chk_at(base + k * 10000LL, &ft, (SYSTEMTIME*)pa, (SYSTEMTIME*)pb, (SYSTEMTIME*)pc,
                   "ms boundary");
            if (k) chk_at(base + k * 10000LL - 1, &ft, (SYSTEMTIME*)pa, (SYSTEMTIME*)pb,
                          (SYSTEMTIME*)pc, "ms boundary - 1");
        }
    }

    /* ---- G. fuzz, fixed seed ----------------------------------------------------------------- */
    for (i = 0; i < 3000000 && fails < 12; ++i) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        chk((long long)(s & 0x7FFFFFFFFFFFFFFFULL));
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        chk((long long)s);                              /* unrestricted: ~half are rejects */
    }

    if (!fails)
        printf("CORRECTNESS: PASS (kernel32!FileTimeToSystemTime vs live + oracle; return, last "
               "error and all 8 fields; %lld cases: EVERY one of the 10,675,200 day boundaries x3, "
               "EVERY one of the 86,400,000 millisecond-of-day quotient boundaries x2, all 8 input "
               "and 16 output byte offsets, page-boundary input and output with PAGE_NOACCESS "
               "beyond, canaries intact, reject path writes nothing, 6M fixed-seed fuzz)\n", cases);
    else
        printf("CORRECTNESS: FAIL (%d mismatches in %lld cases)\n", fails, cases);
    return fails ? 1 : 0;
}
