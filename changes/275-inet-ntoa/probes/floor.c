/* changes/275-inet-ntoa/probes/floor.c
 *
 * Is there anything to win, and what does the buffer cost?
 *
 * discovery/sid_inet_bstr.c measured ws2_32!inet_ntoa at 7.56 ns. That is four numbers of at most
 * three digits and three dots -- change 067's rewrite formats a single 32-bit number in 3.76 ns --
 * so 7.56 ns is not obviously beatable, and that is the question to settle before writing any
 * ASSEMBLY. Change 274 is parked because its short rows turned out to be an allocator call this
 * project does not own; the same trap is here in a different shape.
 *
 * probes/contract.c established that the answer goes in a PER-THREAD buffer -- the same pointer on
 * every call from one thread, a different one per thread, undisturbed by other Winsock calls. So an
 * implementation needs thread-local storage of its own, and on x64 that is either a call to a
 * helper the compiler generates or a hand-written walk of the TLS array. The call is the honest
 * version and it is not free, so it is measured here rather than assumed away.
 *
 * Four things are timed:
 *
 *     ws2_32!inet_ntoa               what we are replacing
 *     a C version, wsprintf          the naive replacement, for scale
 *     a C version, a digit table     what the assembly would be doing
 *     the TLS access ALONE           the floor: every implementation pays it
 *
 * If the third is not comfortably below the first, there is nothing here and the change should be
 * parked before it is written rather than after.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

static __declspec(thread) char tlsbuf[20];

/* the table the assembly would use: three digits and a dot, packed into a dword, plus the length */
static unsigned dot4[256];
static unsigned char dlen[256];

static void build(void)
{
    int i;
    for (i = 0; i < 256; ++i) {
        if (i < 10) {
            dot4[i] = (unsigned)('0' + i) | ((unsigned)'.' << 8);
            dlen[i] = 2;
        } else if (i < 100) {
            dot4[i] = (unsigned)('0' + i / 10) | ((unsigned)('0' + i % 10) << 8) |
                      ((unsigned)'.' << 16);
            dlen[i] = 3;
        } else {
            dot4[i] = (unsigned)('0' + i / 100) | ((unsigned)('0' + (i / 10) % 10) << 8) |
                      ((unsigned)('0' + i % 10) << 16) | ((unsigned)'.' << 24);
            dlen[i] = 4;
        }
    }
}

static char* c_wsprintf(unsigned long net)
{
    unsigned char* b = (unsigned char*)&net;
    wsprintfA(tlsbuf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return tlsbuf;
}

static char* c_table(unsigned long net)
{
    unsigned char* b = (unsigned char*)&net;
    char* p = tlsbuf;
    *(unsigned*)p = dot4[b[0]]; p += dlen[b[0]];
    *(unsigned*)p = dot4[b[1]]; p += dlen[b[1]];
    *(unsigned*)p = dot4[b[2]]; p += dlen[b[2]];
    *(unsigned*)p = dot4[b[3]]; p += dlen[b[3]];
    p[-1] = 0;                          /* the last dot becomes the terminator */
    return tlsbuf;
}

static char* c_tls_only(unsigned long net)
{
    tlsbuf[0] = (char)net;
    return tlsbuf;
}

static double freq;
static volatile unsigned long long sink;

#define REP 4000

static double timeit(char* (*f)(unsigned long), const unsigned long* v, int nv)
{
    LARGE_INTEGER a, b;
    double best = 1e300;
    int pass, i;
    for (pass = 0; pass < 200; ++pass) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < REP; ++i) sink += (unsigned char)f(v[i % nv])[0];
        QueryPerformanceCounter(&b);
        {
            double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq / REP;
            if (ns < best) best = ns;
        }
    }
    return best;
}

static char* sys_ntoa(unsigned long net)
{
    struct in_addr a;
    a.S_un.S_addr = net;
    return inet_ntoa(a);
}

int main(void)
{
    WSADATA wd;
    LARGE_INTEGER f;
    static const unsigned long V[] = {
        0x0100007Ful, 0x04030201ul, 0xFFFFFFFFul, 0x00000000ul,
        0xC8A8A8C0ul, 0x0101010Aul, 0x09090909ul, 0x64646464ul
    };
    static const unsigned long ONE[] = { 0x0100007Ful };
    static const unsigned long SHORT[] = { 0x01010101ul };
    static const unsigned long LONG_[] = { 0xFFFFFFFFul };

    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wd);
    QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;
    build();

    /* the three C versions must agree with the export before any of them is timed */
    {
        int i, bad = 0;
        for (i = 0; i < 1000; ++i) {
            unsigned long a = (unsigned long)i * 2654435761ul;
            char want[32];
            lstrcpynA(want, sys_ntoa(a), 32);
            if (lstrcmpA(c_wsprintf(a), want)) ++bad;
            if (lstrcmpA(c_table(a), want)) ++bad;
        }
        printf("== the two C versions disagree with the export on %d of 2000 checks ==\n\n", bad);
    }

    printf("== nanoseconds per call, min of 200 passes of %d ==\n", REP);
    printf("   %-34s %10s %10s %10s %10s\n", "", "mixed", "127.0.0.1", "1.1.1.1", "255.255.*");
    printf("   %-34s %9.2f  %9.2f  %9.2f  %9.2f\n", "ws2_32!inet_ntoa",
           timeit(sys_ntoa, V, 8), timeit(sys_ntoa, ONE, 1),
           timeit(sys_ntoa, SHORT, 1), timeit(sys_ntoa, LONG_, 1));
    printf("   %-34s %9.2f  %9.2f  %9.2f  %9.2f\n", "C, wsprintfA",
           timeit(c_wsprintf, V, 8), timeit(c_wsprintf, ONE, 1),
           timeit(c_wsprintf, SHORT, 1), timeit(c_wsprintf, LONG_, 1));
    printf("   %-34s %9.2f  %9.2f  %9.2f  %9.2f\n", "C, the digit table",
           timeit(c_table, V, 8), timeit(c_table, ONE, 1),
           timeit(c_table, SHORT, 1), timeit(c_table, LONG_, 1));
    printf("   %-34s %9.2f  %9.2f  %9.2f  %9.2f\n", "the TLS access alone (the floor)",
           timeit(c_tls_only, V, 8), timeit(c_tls_only, ONE, 1),
           timeit(c_tls_only, SHORT, 1), timeit(c_tls_only, LONG_, 1));

    printf("\n   the digit-table version is what the assembly would do, in C and through a\n"
           "   compiler-generated TLS access. If it is not comfortably under the export, there\n"
           "   is nothing here.\n");
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
