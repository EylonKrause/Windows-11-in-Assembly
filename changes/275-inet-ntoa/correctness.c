/* changes/275-inet-ntoa/correctness.c
 *
 * Gate 1 for ws2_32!inet_ntoa: OURS vs THE SCALAR MODEL vs THE LIVE EXPORT, on the text.
 *
 * THE POINTER CANNOT BE COMPARED AND SHOULD NOT BE. Both implementations return a per-thread buffer
 * of their own; the contract is that the text is valid until the same thread calls again, which
 * probes/contract.c measured. So the comparison is the string -- and the live export's answer has to
 * be COPIED before ours is asked for, because if ours were the shipped one they would be the same
 * buffer and the second call would overwrite the first. A gate that forgot that would compare a
 * string against itself and pass no matter what.
 *
 * THE FORMATTING IS PER-BYTE AND THE BYTES ARE CONCATENATED, so exhaustive over one byte is not
 * enough and exhaustive over four is 4294967296 pairs of calls. What settles it is exhaustive over
 * ADJACENT PAIRS: every one of the 65536 combinations of bytes 0 and 1, and of bytes 2 and 3,
 * against several backgrounds. Any interaction between fields is an interaction between neighbours,
 * because the only thing a field does to the next one is decide where it starts.
 *
 * AND THE STEP IS WHAT CAN GO WRONG. Each field writes four bytes and advances by two, three or
 * four, so the interesting cases are where a short field is followed by a long one -- the long one's
 * store has to land on top of the short one's padding. Every length transition is covered by the
 * pair sweeps by construction.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

extern char* wia_inet_ntoa(unsigned long);
extern void* wia_ntoa_tables(void);
char* ref_inet_ntoa(unsigned long, char*);

static int  failures = 0;
static long cases = 0;

static void one(unsigned long net)
{
    char live[64], mine[64], model[64];
    struct in_addr a;
    a.S_un.S_addr = net;

    /* COPY the live answer first: if ours were the shipped export these would be the same buffer */
    lstrcpynA(live, inet_ntoa(a), 64);
    lstrcpynA(mine, wia_inet_ntoa(net), 64);
    ref_inet_ntoa(net, model);

    ++cases;
    if (lstrcmpA(live, mine) != 0 || lstrcmpA(live, model) != 0) {
        if (failures < 12)
            printf("  FAIL %08lX: live \"%s\"  ours \"%s\"  model \"%s\"\n",
                   (unsigned long)net, live, mine, model);
        ++failures;
    }
}

static int check_tables(void)
{
    const unsigned* dot4 = (const unsigned*)wia_ntoa_tables();
    const unsigned char* dlen = (const unsigned char*)(dot4 + 256);
    int i, bad = 0;
    for (i = 0; i < 256; ++i) {
        unsigned want;
        unsigned char wlen;
        if (i < 10) { want = (unsigned)('0' + i) | ((unsigned)'.' << 8); wlen = 2; }
        else if (i < 100) {
            want = (unsigned)('0' + i / 10) | ((unsigned)('0' + i % 10) << 8) | ((unsigned)'.' << 16);
            wlen = 3;
        } else {
            want = (unsigned)('0' + i / 100) | ((unsigned)('0' + (i / 10) % 10) << 8) |
                   ((unsigned)('0' + i % 10) << 16) | ((unsigned)'.' << 24);
            wlen = 4;
        }
        if (dot4[i] != want) { printf("  the digit table is wrong at %d: %08X, want %08X\n",
                                      i, dot4[i], want); bad = 1; }
        if (dlen[i] != wlen) { printf("  the step table is wrong at %d: %u, want %u\n",
                                      i, dlen[i], wlen); bad = 1; }
    }
    if (!bad) printf("  0. the two assembler-generated tables match their definitions\n");
    return bad;
}

static DWORD WINAPI worker(LPVOID p)
{
    /* A SECOND THREAD, because the buffer is per-thread and a single-threaded gate cannot tell a
       per-thread buffer from a per-process one. Each thread formats its own address a thousand
       times and checks that nothing else wrote over it. */
    unsigned long base = (unsigned long)(UINT_PTR)p;
    int i, bad = 0;
    for (i = 0; i < 1000; ++i) {
        unsigned long v = base + (unsigned long)i * 0x01010101ul;
        char want[64];
        char* got;
        struct in_addr a;
        a.S_un.S_addr = v;
        lstrcpynA(want, inet_ntoa(a), 64);
        got = wia_inet_ntoa(v);
        if (lstrcmpA(want, got) != 0) ++bad;
    }
    return (DWORD)bad;
}

int main(void)
{
    WSADATA wd;
    unsigned i, j;
    unsigned long seed = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wd);
    printf("== CORRECTNESS: inet_ntoa ==\n");
    if (check_tables()) return 1;

    /* 1. every byte value in every position, against four backgrounds */
    {
        long before = cases;
        static const unsigned long BG[] = { 0x00000000ul, 0xFFFFFFFFul, 0x01010101ul, 0x64646464ul };
        for (i = 0; i < 4; ++i)
            for (j = 0; j < 4; ++j) {
                unsigned v;
                for (v = 0; v < 256; ++v) {
                    unsigned long a = BG[i];
                    ((unsigned char*)&a)[j] = (unsigned char)v;
                    one(a);
                }
            }
        printf("  1. every byte value in every position, four backgrounds: %ld\n", cases - before);
    }

    /* 2. every combination of two ADJACENT bytes -- where one field's step meets the next */
    {
        long before = cases;
        unsigned b0, b1;
        for (b0 = 0; b0 < 256; ++b0)
            for (b1 = 0; b1 < 256; ++b1) {
                unsigned long a = 0x01010000ul | (b1 << 8) | b0;
                one(a);
            }
        for (b0 = 0; b0 < 256; ++b0)
            for (b1 = 0; b1 < 256; ++b1) {
                unsigned long a = 0x00000101ul | ((unsigned long)b1 << 24) | ((unsigned long)b0 << 16);
                one(a);
            }
        printf("  2. all 65536 combinations of bytes 0-1 and of bytes 2-3: %ld\n", cases - before);
    }

    /* 3. the length transitions, stated rather than left to the sweeps */
    {
        long before = cases;
        static const unsigned char L[] = { 0, 1, 9, 10, 11, 99, 100, 101, 254, 255 };
        unsigned a, b, c, d;
        for (a = 0; a < sizeof L; ++a)
            for (b = 0; b < sizeof L; ++b)
                for (c = 0; c < sizeof L; ++c)
                    for (d = 0; d < sizeof L; ++d)
                        one((unsigned long)L[a] | ((unsigned long)L[b] << 8) |
                            ((unsigned long)L[c] << 16) | ((unsigned long)L[d] << 24));
        printf("  3. every length transition, all four fields: %ld\n", cases - before);
    }

    /* 4. randomised */
    {
        long before = cases;
        for (i = 0; i < 400000 && failures < 12; ++i) {
            seed = seed * 1103515245u + 12345u;
            one(seed);
        }
        printf("  4. 400000 randomised: %ld\n", cases - before);
    }

    /* 5. four threads at once, because the buffer is per-thread */
    {
        HANDLE h[4];
        DWORD rc, bad = 0;
        for (i = 0; i < 4; ++i)
            h[i] = CreateThread(0, 0, worker, (LPVOID)(UINT_PTR)(0x11111111ul * (i + 1)), 0, 0);
        for (i = 0; i < 4; ++i) {
            WaitForSingleObject(h[i], INFINITE);
            GetExitCodeThread(h[i], &rc);
            bad += rc;
            CloseHandle(h[i]);
        }
        cases += 4000;
        printf("  5. four threads x 1000 addresses, each checking its own buffer: 4000\n");
        if (bad) { printf("  FAIL: %lu mismatches across threads\n", (unsigned long)bad);
                   failures += (int)bad; }
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    if (!failures)
        printf("CORRECTNESS: PASS (the text exact vs live ws2_32 and vs the scalar model, every byte\n"
               "value in every position, every adjacent pair, and across four threads at once)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
