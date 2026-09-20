/* changes/272-convertstringsidtosida/bench.c
 *
 * Gate 2: time wia_str2sida against the live advapi32!ConvertStringSidToSidA.
 *
 * Every row allocates and frees on both sides, because the contract returns a LocalAlloc block the
 * caller frees.
 *
 * The rows are the shapes the implementation distinguishes. Two of them are this change's own and
 * exist nowhere in change 269:
 *
 *   "a high byte"     an input containing a byte at or above 0x80, which is the only path that
 *                     consults the code page. Without this row the fallback is never timed, and a
 *                     fallback that were slower than the shipped export would land unnoticed --
 *                     which is exactly the defect class change 210 and change 269's first bench had.
 *   "1024 characters" an input past the point where the widened copy stops being the frame and
 *                     starts being an allocation.
 *
 * Every row is pre-flighted against a per-row table of what the live export must return.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

extern BOOL wia_str2sida(const char*, PSID*);
int wia_sid_alias_init(void);
int wia_sid_classify_init(void);

typedef BOOL (WINAPI *FN)(LPCSTR, PSID*);
static FN sys;

typedef struct { const char* s; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) {
        PSID p = 0;
        if (wia_str2sida(m->s, &p) && p) { acc += *(unsigned char*)p; LocalFree(p); }
        else acc += 1;
    }
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) {
        PSID p = 0;
        if (sys(m->s, &p) && p) { acc += *(unsigned char*)p; LocalFree(p); }
        else acc += 1;
    }
    return acc;
}

enum { K = 12 };

int main(void)
{
    static char longsid[4096], highbyte[64];
    static const char* S[K];
    static const char* N[K] = {
        "1 sub-authority", "2 sub-authorities", "5, a real account SID",
        "8 sub-authorities", "10 sub-authorities", "the alias BA", "the alias LA (machine)",
        "hex carry, 6 fields", "a high byte (code page)", "1024 characters (allocates)",
        "a refusal, late", "a refusal, immediate"
    };
    static const int WANT_OK[K] = { 1,1,1,1,1,1,1,1, 0, 1, 0,0 };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0, k;

    sys = (FN)GetProcAddress(LoadLibraryW(L"advapi32.dll"), "ConvertStringSidToSidA");
    if (!sys) { printf("no ConvertStringSidToSidA\n"); return 1; }
    if (wia_sid_classify_init()) { printf("the character classes failed to build\n"); return 1; }
    if (wia_sid_alias_init())    { printf("the alias table failed to build\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Nine-digit sub-authorities, not one-digit ones. The first version of this row filled 1020
       characters with "-1"-style fields, which is 500-odd sub-authorities -- and the limit is 254,
       so the row was an ERROR_ARITHMETIC_OVERFLOW refusal wearing the name of the longest ACCEPTED
       input. The pre-flight below refused to benchmark it, which is the whole reason the pre-flight
       exists: change 210 shipped a row that never reached its own path, and so did change 269's
       first bench. A hundred nine-digit fields is 1005 characters and 100 sub-authorities. */
    k = wsprintfA(longsid, "S-1-5");
    while (k < 1000) k += wsprintfA(longsid + k, "-%09d", (k % 9) + 1);
    wsprintfA(highbyte, "S-1-5-21-305419896-2596069104-287454020-10\xE9""1");

    S[0]  = "S-1-5-1";
    S[1]  = "S-1-5-1-2";
    S[2]  = "S-1-5-21-305419896-2596069104-287454020-1001";
    S[3]  = "S-1-5-21-1-2-3-4-5-6";
    S[4]  = "S-1-5-21-1-2-3-4-5-6-7-8";
    S[5]  = "BA";
    S[6]  = "LA";
    S[7]  = "S-0x1-5-21-1a2b-3c4d-5e6f-1001";
    S[8]  = highbyte;
    S[9]  = longsid;
    S[10] = "S-1-5-21-305419896-2596069104-287454020-100x";
    S[11] = "not-a-sid-at-all";

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < K; ++i) {
        PSID p = 0;
        BOOL r;
        SetLastError(0);
        r = sys(S[i], &p);
        printf("    %-30s %s  %d bytes in", N[i], r ? "ACCEPTED" : "refused ", lstrlenA(S[i]));
        if (r && p) { printf(", %lu-byte SID", (unsigned long)GetLengthSid(p)); LocalFree(p); }
        else        { printf(", err=%lu", (unsigned long)GetLastError()); }
        printf("\n");
        if ((r ? 1 : 0) != WANT_OK[i]) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            ++bad;
        }
        cx[i].s = S[i];
        cx[i].reps = (i == 9) ? 1 : 8;        /* the long row is slow enough to stand alone */
        cs[i].label = N[i];
        cs[i].bytes = (size_t)lstrlenA(S[i]) * (size_t)cx[i].reps;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("advapi32 ConvertStringSidToSidA  (wia: an ASCII zero-extension over "
                             "change 269, vs the shipped wrapper)", cs, K, 200);
}
