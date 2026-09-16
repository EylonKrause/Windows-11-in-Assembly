/* changes/275-inet-ntoa/bench.c
 *
 * Gate 2: time wia_inet_ntoa against the live ws2_32!inet_ntoa.
 *
 * EVERY ROW IS TIMED x16. probes/floor.c put the digit-table version at 1.43 ns a call and change
 * 261's probes/floor.c put an empty call through THIS harness at 2.32 ns -- so a row of one call
 * would be measuring the harness and reporting it as the function. Sixteen puts every row above
 * twenty nanoseconds.
 *
 * THE ROWS ARE THE FOUR FIELD LENGTHS AND THEIR MIXTURES, because the implementation steps by two,
 * three or four bytes per field and the only thing that varies is how often it takes each. A row of
 * "127.0.0.1" alone would measure one of the three step lengths three times.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "bench.h"

#pragma comment(lib, "ws2_32.lib")

extern char* wia_inet_ntoa(unsigned long);

#define REPS 16

typedef struct { unsigned long v; } ctx_t;

static uint64_t op_ours(void* c)
{
    unsigned long v = ((ctx_t*)c)->v;
    uint64_t acc = 0;
    int i;
    for (i = 0; i < REPS; ++i) acc += (unsigned char)wia_inet_ntoa(v)[0];
    return acc;
}
static uint64_t op_sys(void* c)
{
    unsigned long v = ((ctx_t*)c)->v;
    uint64_t acc = 0;
    int i;
    struct in_addr a;
    a.S_un.S_addr = v;
    for (i = 0; i < REPS; ++i) acc += (unsigned char)inet_ntoa(a)[0];
    return acc;
}

enum { K = 8 };

int main(void)
{
    WSADATA wd;
    static const unsigned long V[K] = {
        0x01010101ul,   /* 1.1.1.1              four one-digit fields */
        0x0A0A0A0Aul,   /* 10.10.10.10          four two-digit fields */
        0xFFFFFFFFul,   /* 255.255.255.255      four three-digit fields */
        0x0100007Ful,   /* 127.0.0.1            a real one: 3,1,1,1 */
        0xC8A8A8C0ul,   /* 192.168.168.200      3,3,3,3 */
        0x0101A8C0ul,   /* 192.168.1.1          3,3,1,1 */
        0x00000000ul,   /* 0.0.0.0              the shortest answer */
        0x64020A0Aul    /* 10.10.2.100          a mixture of all three */
    };
    static char names[K][24];
    static const char* N[K];
    static ctx_t cx[K];
    static wia_case cs[K];
    int i;

    WSAStartup(MAKEWORD(2, 2), &wd);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("  pre-flight (what each row reaches):\n");
    for (i = 0; i < K; ++i) {
        struct in_addr a;
        char t[32];
        a.S_un.S_addr = V[i];
        lstrcpynA(t, inet_ntoa(a), 32);
        wsprintfA(names[i], "%s", t);
        N[i] = names[i];
        printf("    %-20s %d characters\n", N[i], lstrlenA(t));
        cx[i].v = V[i];
        cs[i].label = N[i];
        cs[i].bytes = (size_t)lstrlenA(t) * REPS;
        cs[i].ours = op_ours; cs[i].system = op_sys; cs[i].ctx = &cx[i];
    }

    return wia_bench_compare("ws2_32 inet_ntoa  (wia vs the shipped formatter, x16 calls per row)",
                             cs, K, 300);
}
