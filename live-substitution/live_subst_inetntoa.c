// live-substitution/live_subst_inetntoa.c
// LIVE-RUN PROOF for change 275 (ws2_32!inet_ntoa).
//
// THE ANSWER IS COPIED BEFORE THE NEXT CALL, and that is not a detail. inet_ntoa returns a pointer
// into a PER-THREAD buffer that the next call overwrites -- probes/contract.c measured it: the same
// pointer every time from one thread, a different one per thread. A harness that recorded the
// pointer and compared it later would be comparing a string against whatever the most recent call
// left there, and would pass no matter what either implementation did.
//
// THE POINTER ITSELF CANNOT BE COMPARED AND SHOULD NOT BE. Our implementation has a thread-local
// buffer of its own, which is exactly what the contract permits: the caller owns nothing and the
// text is valid until the same thread calls again. So the comparison is the TEXT.
//
// AND IT RUNS ON FOUR THREADS. A single-threaded harness cannot tell a per-thread buffer from a
// per-process one, and "per-thread" is the only part of this contract that a wrong implementation
// could satisfy on one thread and break on two. Each thread formats its own stream of addresses
// under the patch and checks every one against what it recorded before the patch existed.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone; it patches only its own copy-on-write copy of ws2_32.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: the worker threads are created AFTER the patch is in place and joined
//       BEFORE it is removed, so no thread is ever inside the sixteen bytes being written.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_inetntoa_live.bat
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

#pragma comment(lib, "ws2_32.lib")

typedef char* (WSAAPI *F_NTOA)(struct in_addr);

extern char* wia_inet_ntoa(unsigned long);

static volatile LONG c_hit;
static char* WSAAPI w_ntoa(struct in_addr a)
{ _InterlockedIncrement(&c_hit); return wia_inet_ntoa(a.S_un.S_addr); }

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;

static void raw_copy(volatile unsigned char* d, const volatile unsigned char* s, int n)
{ int i; for (i = 0; i < n; ++i) d[i] = s[i]; }

static int patch_on(patch_t* p, void* target, void* repl)
{
    DWORD old; unsigned char stub[14];
    p->target = target; p->on = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    stub[0] = 0xFF; stub[1] = 0x25; *(uint32_t*)(stub + 2) = 0; *(uint64_t*)(stub + 6) = (uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p)
{
    DWORD old; int i;
    if (!p->on) return 1;
    VirtualProtect(p->target, 16, PAGE_EXECUTE_READWRITE, &old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for (i = 0; i < 16; ++i) if (((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

static int failures = 0;
#define OK(c, m) do { if (!(c)) { printf("  FAIL: %s\n", (m)); ++failures; } } while (0)

#define NCASE  40000
#define NTHREAD 4
#define NPER   4000

static F_NTOA live;
static char expected[NCASE][16];

/* the case is a pure function of its index -- change 252's harness carried PRNG state across its
   passes and reported 14285 differences with its patch counter at ZERO */
static unsigned long addr_of(long i)
{
    unsigned long long rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    switch (i % 5) {
    case 0:  return (unsigned long)rs;                       /* anything */
    case 1:  return 0x01010101ul;                            /* four one-digit fields */
    case 2:  return 0xFFFFFFFFul;                            /* four three-digit fields */
    case 3:  return (unsigned long)(rs & 0x0F0F0F0Ful);      /* short fields */
    default: return (unsigned long)(rs | 0xC0C0C0C0ul);      /* long fields */
    }
}

typedef struct { int id; long bad; } warg_t;

static DWORD WINAPI worker(LPVOID p)
{
    warg_t* w = (warg_t*)p;
    long i;
    w->bad = 0;
    for (i = 0; i < NPER; ++i) {
        long idx = (long)w->id * NPER + i;
        struct in_addr a;
        char got[16];
        a.S_un.S_addr = addr_of(idx);
        /* COPY IT AT ONCE: the next call on this thread overwrites the buffer */
        lstrcpynA(got, live(a), 16);
        if (lstrcmpA(got, expected[idx]) != 0) ++w->bad;
    }
    return 0;
}

int main(void)
{
    WSADATA wd;
    HMODULE hw;
    patch_t pt;
    long i;
    long n_short = 0, n_long = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wd);
    hw = LoadLibraryW(L"ws2_32.dll");
    live = (F_NTOA)GetProcAddress(hw, "inet_ntoa");
    if (!live) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ws2_32!inet_ntoa (change 275) ==\n");
    printf("  the export resolves to %p\n", (void*)live);

    for (i = 0; i < NCASE; ++i) {
        struct in_addr a;
        a.S_un.S_addr = addr_of(i);
        lstrcpynA(expected[i], live(a), 16);
        if (lstrlenA(expected[i]) <= 9) ++n_short;
        if (lstrlenA(expected[i]) >= 14) ++n_long;
    }
    printf("  [pre-patch]  %d answers recorded from the SHIPPED export;  %ld are 9 characters or\n"
           "               fewer and %ld are 14 or more -- the two ends of the step table\n",
           NCASE, n_short, n_long);
    OK(n_short > 5000, "the corpus rarely produced short fields");
    OK(n_long  > 5000, "the corpus rarely produced long fields");

    {
        long differ = 0;
        char got[16];
        if (!patch_on(&pt, (void*)live, (void*)w_ntoa)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            struct in_addr a;
            a.S_un.S_addr = addr_of(i);
            lstrcpynA(got, live(a), 16);
            if (lstrcmpA(got, expected[i]) != 0) {
                if (++differ <= 8)
                    printf("  differ at %ld (%08lX): live \"%s\"  ours \"%s\"\n",
                           i, (unsigned long)addr_of(i), expected[i], got);
            }
        }
        printf("  [patched]    %d cases on this thread, %ld differ;  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "the export answered differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");

        /* FOUR THREADS, created under the patch and joined before it is removed. A per-thread
           buffer is the one part of this contract a wrong implementation could get right on one
           thread and wrong on two. */
        {
            HANDLE h[NTHREAD];
            warg_t w[NTHREAD];
            long bad = 0;
            LONG before = c_hit;
            for (i = 0; i < NTHREAD; ++i) {
                w[i].id = (int)i; w[i].bad = 0;
                h[i] = CreateThread(0, 0, worker, &w[i], 0, 0);
            }
            for (i = 0; i < NTHREAD; ++i) {
                WaitForSingleObject(h[i], INFINITE);
                bad += w[i].bad;
                CloseHandle(h[i]);
            }
            printf("  [patched]    %d threads x %d addresses, %ld differ;  our-code calls = %ld\n",
                   NTHREAD, NPER, bad, (long)(c_hit - before));
            OK(bad == 0, "a worker thread got a different answer -- the buffer is not per-thread");
            OK(c_hit - before == (LONG)(NTHREAD * NPER), "the threads did not all reach our code");
        }

        OK(patch_off(&pt), "the prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG before = c_hit;
        char got[16];
        for (i = 0; i < NCASE; ++i) {
            struct in_addr a;
            a.S_un.S_addr = addr_of(i);
            lstrcpynA(got, live(a), 16);
            if (lstrcmpA(got, expected[i]) != 0) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n",
               NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    if (failures)
        printf("\nLIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    else
        printf("\nLIVE SUBSTITUTION: PASS (the text identical over %d cases on this thread and\n"
               "%d more across four threads at once, each copying its answer before the next call\n"
               "could overwrite it; prologue restored byte-exact and the corpus re-run through it)\n",
               NCASE, NTHREAD * NPER);
    return failures ? 1 : 0;
}
