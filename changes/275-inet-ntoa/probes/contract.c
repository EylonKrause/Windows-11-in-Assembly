/* changes/275-inet-ntoa/probes/contract.c
 *
 * WHERE DOES inet_ntoa PUT ITS ANSWER, AND WHOSE BUFFER IS IT?
 *
 * discovery/sid_inet_bstr.c measured it at 7.56 ns for a four-byte address. That is four numbers, at
 * most three digits each, and three dots -- change 067's rewrite formats a 32-bit number in 3.76 ns,
 * so 7.56 ns for four small ones is not obviously beatable and the contract had better be cheap.
 *
 * THE CONTRACT IS THE BUFFER. inet_ntoa returns `char*` and the documentation says the storage is
 * allocated by Winsock and is per-thread, freed when the thread ends -- which is the only reason
 * this function is convertible at all where SysAllocString was not (change 274 is parked because its
 * allocator is private and cannot be imitated). If the buffer is a plain thread-local array, an
 * implementation can have one of its own and nothing observable changes. If it is something else --
 * a slot in a Winsock per-thread structure that other exports also use, or a per-process buffer --
 * then a replacement that keeps its own would be observably different to a caller that holds the
 * pointer across other Winsock calls.
 *
 * SO THE QUESTIONS ARE ABOUT THE POINTER, NOT THE TEXT:
 *
 *   1. Is the returned pointer the SAME on every call from one thread?
 *   2. Is it DIFFERENT between threads?
 *   3. Does the previous answer survive an unrelated Winsock call, or does something else write
 *      over it?
 *   4. Does it need WSAStartup, and what happens without it?
 *   5. And only then: the text -- every byte value in every position, and what it does with the
 *      four bytes it is given, which is a struct passed BY VALUE and so is really a 32-bit integer.
 *
 * Nothing is asserted. Every line prints what the live export returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

static struct in_addr mk(unsigned long net)
{
    struct in_addr a;
    a.S_un.S_addr = net;
    return a;
}

static DWORD WINAPI other_thread(LPVOID p)
{
    char** out = (char**)p;
    out[0] = inet_ntoa(mk(0x04030201));
    out[1] = inet_ntoa(mk(0x08070605));
    return 0;
}

int main(void)
{
    WSADATA wd;
    char* p[8];

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== 0. before WSAStartup ==\n");
    {
        char* a = inet_ntoa(mk(0x0100007F));
        printf("   inet_ntoa(127.0.0.1) -> %p  \"%s\"   WSAGetLastError=%d\n",
               (void*)a, a ? a : "(null)", WSAGetLastError());
    }

    if (WSAStartup(MAKEWORD(2, 2), &wd)) { printf("WSAStartup failed\n"); return 1; }

    printf("\n== 1. is the pointer the same on every call from one thread? ==\n");
    p[0] = inet_ntoa(mk(0x0100007F));
    printf("   call 1 -> %p  \"%s\"\n", (void*)p[0], p[0]);
    p[1] = inet_ntoa(mk(0x04030201));
    printf("   call 2 -> %p  \"%s\"   (and call 1's text is now \"%s\")\n",
           (void*)p[1], p[1], p[0]);
    printf("   %s\n", p[0] == p[1] ? "   -- the SAME buffer, so the caller must copy it"
                                   : "   -- DIFFERENT buffers");

    printf("\n== 2. is it per-thread? ==\n");
    {
        char* q[2] = { 0, 0 };
        HANDLE h = CreateThread(0, 0, other_thread, q, 0, 0);
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
        printf("   this thread  %p\n   other thread %p   (and %p on its second call)\n",
               (void*)p[1], (void*)q[0], (void*)q[1]);
        printf("   %s\n", (q[0] && q[0] != p[1]) ? "   -- PER-THREAD storage"
                                                 : "   -- the SAME buffer across threads");
        printf("   after the other thread ended, our text is still \"%s\"\n", p[1]);
    }

    printf("\n== 3. does an unrelated Winsock call disturb it? ==\n");
    {
        char before[32];
        char* a = inet_ntoa(mk(0x0A0B0C0D));
        lstrcpynA(before, a, 32);
        {
            /* three unrelated calls that all use per-thread Winsock state */
            unsigned long x = inet_addr("1.2.3.4");
            struct in_addr t; t.S_un.S_addr = x;
            (void)htonl(0x12345678);
            (void)WSAGetLastError();
            (void)t;
        }
        printf("   \"%s\" before, \"%s\" after inet_addr/htonl/WSAGetLastError -- %s\n",
               before, a, lstrcmpA(before, a) == 0 ? "UNCHANGED" : "OVERWRITTEN");
        {
            struct in_addr t = mk(0x01010101);
            const char* b = inet_ntop(AF_INET, &t, before, 32);
            (void)b;
        }
        a = inet_ntoa(mk(0x0A0B0C0D));
        printf("   and inet_ntop does not share it: \"%s\"\n", a);
    }

    printf("\n== 4. the text: what does it do with each of the four bytes? ==\n");
    {
        static const unsigned long V[] = {
            0x00000000ul, 0x01010101ul, 0xFFFFFFFFul, 0x0100007Ful,
            0x0000000Aul, 0x0A000000ul, 0x00FF0000ul, 0x000000FFul,
            0x63636363ul, 0x0A0A0A0Aul, 0x09090909ul, 0x64646464ul
        };
        unsigned i;
        for (i = 0; i < sizeof V / sizeof V[0]; ++i) {
            unsigned char* b = (unsigned char*)&V[i];
            printf("   %08lX  bytes %3u.%3u.%3u.%3u  ->  \"%s\"\n",
                   (unsigned long)V[i], b[0], b[1], b[2], b[3], inet_ntoa(mk(V[i])));
        }
    }

    printf("\n== 5. the longest and shortest answers, and the buffer's size ==\n");
    {
        char* a = inet_ntoa(mk(0xFFFFFFFFul));
        char* z = inet_ntoa(mk(0x00000000ul));
        printf("   255.255.255.255 is %d characters; 0.0.0.0 is %d\n",
               lstrlenA(a), lstrlenA(z));
        a = inet_ntoa(mk(0xFFFFFFFFul));
        printf("   the sixteen bytes at the buffer after the longest answer: ");
        { int i; for (i = 0; i < 17; ++i) printf("%02X ", (unsigned char)a[i]); }
        printf("\n");
        z = inet_ntoa(mk(0x00000000ul));
        printf("   and after the shortest:                                   ");
        { int i; for (i = 0; i < 17; ++i) printf("%02X ", (unsigned char)z[i]); }
        printf("\n   (if the tail of the long answer survives the short one, the buffer is not\n"
               "    cleared and a reimplementation must leave the same residue)\n");
    }

    printf("\n== 6. every one of the 256 byte values, in each of the four positions ==\n");
    {
        int pos, v, bad = 0;
        for (pos = 0; pos < 4; ++pos) {
            for (v = 0; v < 256; ++v) {
                unsigned long a = 0x01010101ul;
                char want[32];
                unsigned char* b;
                ((unsigned char*)&a)[pos] = (unsigned char)v;
                b = (unsigned char*)&a;
                wsprintfA(want, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
                if (lstrcmpA(inet_ntoa(mk(a)), want) != 0) {
                    if (bad < 8)
                        printf("   position %d, value %3d: \"%s\" but wsprintf says \"%s\"\n",
                               pos, v, inet_ntoa(mk(a)), want);
                    ++bad;
                }
            }
        }
        printf("   %d disagreement(s) with \"%%u.%%u.%%u.%%u\" over all 1024 combinations\n", bad);
    }
    return 0;
}
