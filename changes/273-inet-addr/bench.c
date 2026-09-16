/* changes/273-inet-addr/bench.c
 *
 * Gate 2: time wia_inet_addr against the live ws2_32!inet_addr.
 *
 * discovery/sid_inet_bstr.c measured the shipped export at 24.98 ns, which is close enough to the
 * harness floor that every row is timed x8 (change 261's probes/floor.c put an empty call through
 * this harness at 2.32 ns).
 *
 * THE ROWS ARE THE SHAPES THE IMPLEMENTATION DISTINGUISHES -- the four forms, the three bases, the
 * two ways a parse can end, and the paths that exist only because of what the probes found:
 *
 *   "wrapping accumulator"   a number long enough to overflow 32 bits and still be accepted, which
 *                            is the rule no description of inet_addr contains
 *   "whitespace then junk"   an address followed by text that is never looked at
 *   "a refusal, late"        a well-formed address whose last part is out of range
 *
 * EVERY ROW IS PRE-FLIGHTED against a per-row table of what the live export must return, because a
 * row named for a path it does not reach times something else under that name.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

#pragma comment(lib, "ws2_32.lib")

extern unsigned long wia_inet_addr(const char*);

typedef struct { const char* s; int reps; } ctx_t;

static uint64_t op_ours(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += wia_inet_addr(m->s);
    return acc;
}
static uint64_t op_sys(void* c)
{
    ctx_t* m = (ctx_t*)c;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < m->reps; ++i) acc += inet_addr(m->s);
    return acc;
}

enum { K = 13 };

int main(void)
{
    WSADATA wd;
    static const char* S[K] = {
        "1.2.3.4",
        "192.168.100.200",
        "255.255.255.254",
        "10.0.0.1",
        "1.2.3",
        "1.2",
        "16909060",
        "0x7f000001",
        "017700000001",
        "0x112345678",
        "1.2.3.4 and some trailing text nobody reads",
        "1.2.3.256",
        "not-an-address"
    };
    static const char* N[K] = {
        "1.2.3.4", "192.168.100.200", "255.255.255.254", "10.0.0.1",
        "three parts", "two parts", "one part, decimal",
        "one part, hexadecimal", "one part, octal",
        "wrapping accumulator", "whitespace then junk",
        "a refusal, late", "a refusal, immediate"
    };
    /* 0 = must be refused (INADDR_NONE), 1 = must be accepted */
    static const int WANT_OK[K] = { 1,1,1,1,1,1,1,1,1,1,1, 0,0 };
    static ctx_t cx[K];
    static wia_case cs[K];
    int i, bad = 0;

    WSAStartup(MAKEWORD(2, 2), &wd);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < K; ++i) {
        unsigned long v = inet_addr(S[i]);
        int ok = (v != INADDR_NONE);
        printf("    %-24s %-8s %08lX   \"%.34s\"\n", N[i], ok ? "ACCEPTED" : "refused ",
               (unsigned long)v, S[i]);
        if (ok != WANT_OK[i]) {
            printf("      ^^ THIS ROW DOES NOT REACH THE PATH ITS NAME PROMISES\n");
            ++bad;
        }
        cx[i].s = S[i];
        cx[i].reps = 8;                 /* the shipped export is ~25 ns; x8 clears the 2.32 ns floor */
        cs[i].label = N[i];
        cs[i].bytes = (size_t)lstrlenA(S[i]) * 8;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }
    if (bad) { printf("  %d row(s) mis-named -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ws2_32 inet_addr  (wia vs the shipped parser, x8 calls per row)",
                             cs, K, 200);
}
