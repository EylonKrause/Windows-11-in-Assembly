/* changes/067-rtlconvertsidtounicodestring/bench.c
 *
 * Gate 2: time wia_sidfmt against the live ntdll!RtlConvertSidToUnicodeString.
 *
 * The old bench was one row, built /Od, and it hid the whole point of the function. It formatted a
 * single five-sub-authority SID and printed 1.42x. What it could not show is that this function's
 * cost is almost entirely the NUMBER CONVERSIONS -- so the rows that matter are the ones that vary
 * how many numbers there are and how many digits each has, and the row that has none at all.
 *
 *   count 0, 1, 2, 5, 8, 15    the per-number cost, which is what the rewrite changed
 *   short sub-authorities      1-3 digits each: the same fifteen numbers, a third of the digits
 *   the hexadecimal authority  a different converter, reached only above 2^32
 *   three refusals             a bad revision, a count above 15, and a destination too small --
 *                              paths a caller takes as often as the succeeding one, and which no
 *                              "how fast does it format" row measures
 *
 * Every row is pre-flighted. a row named for a path it does not reach is timing something else
 * under that name -- change 210 shipped a "table path" row that short-circuited in tier one, and
 * change 269's first bench had a "Unicode digits" row that was a refusal. The table below states
 * per row what it must return, and the bench refuses to run if the live export disagrees.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;

extern NTSTATUS wia_sidfmt(U*, void*, BOOLEAN);
typedef NTSTATUS (WINAPI *fn)(U*, void*, BOOLEAN);
static fn sys;

typedef struct { unsigned char sid[8 + 4 * 16]; USHORT ml; wchar_t buf[256]; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    U s;
    s.Length = 0; s.MaximumLength = m->ml; s.Buffer = m->buf;
    return (uint64_t)(unsigned)wia_sidfmt(&s, m->sid, FALSE) + s.Length;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    U s;
    s.Length = 0; s.MaximumLength = m->ml; s.Buffer = m->buf;
    return (uint64_t)(unsigned)sys(&s, m->sid, FALSE) + s.Length;
}

static void mk(unsigned char* sid, unsigned rev, unsigned long long auth,
               unsigned cnt, const unsigned* sub)
{
    unsigned i;
    sid[0] = (unsigned char)rev;
    sid[1] = (unsigned char)cnt;
    for (i = 0; i < 6; ++i) sid[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < cnt && i < 16; ++i) {
        sid[8 + 4 * i + 0] = (unsigned char)(sub[i]);
        sid[8 + 4 * i + 1] = (unsigned char)(sub[i] >> 8);
        sid[8 + 4 * i + 2] = (unsigned char)(sub[i] >> 16);
        sid[8 + 4 * i + 3] = (unsigned char)(sub[i] >> 24);
    }
}

enum { K = 11 };

int main(void)
{
    static const char* N[K] = {
        "0 sub-authorities", "1 sub-authority", "2 sub-authorities",
        "5, a real account SID", "8 sub-authorities", "15, the maximum",
        "15, short (1-3 digits)", "hex authority, 5 subs",
        "a refusal: revision 2", "a refusal: count 16", "a refusal: no room"
    };
    /* 0 = this row must SUCCEED, else the NTSTATUS it must return */
    static const NTSTATUS WANT[K] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        (NTSTATUS)0xC0000078, (NTSTATUS)0xC0000078, (NTSTATUS)0x80000005
    };
    static ctx_t cx[K];
    static wia_case cs[K];
    static unsigned big[16], small[16];
    int i, bad = 0;

    sys = (fn)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlConvertSidToUnicodeString");
    if (!sys) { printf("no RtlConvertSidToUnicodeString\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 16; ++i) { big[i] = 1000000000u + (unsigned)i * 271828183u; small[i] = 1 + i * 37; }
    big[0] = 21; big[1] = 305419896u; big[2] = 2596069104u; big[3] = 287454020u; big[4] = 1001;

    for (i = 0; i < K; ++i) { cx[i].ml = 512; }
    mk(cx[0].sid, 1, 5, 0,  big);
    mk(cx[1].sid, 1, 5, 1,  big);
    mk(cx[2].sid, 1, 5, 2,  big);
    mk(cx[3].sid, 1, 5, 5,  big);
    mk(cx[4].sid, 1, 5, 8,  big);
    mk(cx[5].sid, 1, 5, 15, big);
    mk(cx[6].sid, 1, 5, 15, small);
    mk(cx[7].sid, 1, 0x123456789ABCull, 5, big);
    mk(cx[8].sid, 2, 5, 5,  big);
    mk(cx[9].sid, 1, 5, 5,  big); cx[9].sid[1] = 16;
    mk(cx[10].sid, 1, 5, 15, big); cx[10].ml = 16;      /* far too small for a 15-sub-authority SID */

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < K; ++i) {
        wchar_t b[256];
        U s;
        NTSTATUS r;
        s.Length = 0; s.MaximumLength = cx[i].ml; s.Buffer = b;
        r = sys(&s, cx[i].sid, FALSE);
        printf("    %-24s %08lX  %u chars\n", N[i], (unsigned long)r, s.Length / 2);
        if (r != WANT[i]) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES (wanted %08lX)\n",
                   (unsigned long)WANT[i]);
            ++bad;
        }
        cs[i].label = N[i];
        cs[i].bytes = (r == 0) ? s.Length : 8;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("RtlConvertSidToUnicodeString  (wia vs ntdll)", cs, K, 400);
}
