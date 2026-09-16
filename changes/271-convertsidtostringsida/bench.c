/* changes/271-convertsidtostringsida/bench.c
 *
 * Gate 2: time wia_sid2stra against the live advapi32!ConvertSidToStringSidA.
 *
 * EVERY ROW ALLOCATES AND FREES on both sides, because the contract returns a LocalAlloc block the
 * caller frees and discovery/sid_inet_bstr.c measured that pair at 40.41 ns.
 *
 * THE ROWS ARE THE SHAPES THE IMPLEMENTATION DISTINGUISHES -- the counts that set how many numbers
 * there are, the hexadecimal authority that is a different converter, and the two refusals a caller
 * hits as often as the succeeding case. The count rows also walk the 16-character pack boundary: 5
 * characters takes the byte loop, 44 takes two full iterations and an overlapping tail, and 152
 * takes nine.
 *
 * EVERY ROW IS PRE-FLIGHTED against a per-row table of what the live export must return.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

extern BOOL wia_sid2stra(const void*, char**);
typedef BOOL (WINAPI *FN)(PSID, LPSTR*);
static FN sys;

typedef struct { unsigned char sid[8 + 4 * 16]; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    char* p = 0;
    if (wia_sid2stra(m->sid, &p) && p) { uint64_t v = (uint64_t)(unsigned char)p[0]; LocalFree(p); return v; }
    return 1;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    LPSTR p = 0;
    if (sys((PSID)m->sid, &p) && p) { uint64_t v = (uint64_t)(unsigned char)p[0]; LocalFree(p); return v; }
    return 1;
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

enum { K = 10 };

int main(void)
{
    static const char* N[K] = {
        "0 subs (5 chars, byte loop)", "1 sub-authority", "2 sub-authorities",
        "5, a real account SID", "8 sub-authorities", "15, the maximum",
        "15, short (1-3 digits)", "hex authority, 5 subs",
        "a refusal: revision 2", "a refusal: count 16"
    };
    static const int WANT_OK[K] = { 1,1,1,1,1,1,1,1, 0,0 };
    static ctx_t cx[K];
    static wia_case cs[K];
    static unsigned big[16], small[16];
    int i, bad = 0;

    sys = (FN)GetProcAddress(LoadLibraryW(L"advapi32.dll"), "ConvertSidToStringSidA");
    if (!sys) { printf("no ConvertSidToStringSidA\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 16; ++i) { big[i] = 1000000000u + (unsigned)i * 271828183u; small[i] = 1 + i * 37; }
    big[0] = 21; big[1] = 305419896u; big[2] = 2596069104u; big[3] = 287454020u; big[4] = 1001;

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

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < K; ++i) {
        LPSTR p = 0;
        BOOL r;
        SetLastError(0);
        r = sys((PSID)cx[i].sid, &p);
        printf("    %-30s %s", N[i], r ? "ACCEPTED" : "refused ");
        if (r && p) printf("  %d chars", lstrlenA(p));
        else        printf("  err=%lu", (unsigned long)GetLastError());
        printf("\n");
        if ((r ? 1 : 0) != WANT_OK[i]) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            ++bad;
        }
        cs[i].label = N[i];
        cs[i].bytes = (r && p) ? (size_t)lstrlenA(p) : 8;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
        if (p) LocalFree(p);
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("advapi32 ConvertSidToStringSidA  (wia envelope over change 067, "
                             "narrowed with VPACKUSWB, vs the shipped wrapper)", cs, K, 300);
}
