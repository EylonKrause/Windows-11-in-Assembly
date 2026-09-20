// live-substitution/live_subst_iszero.c
// LIVE-RUN PROOF for change 266 (ntdll!RtlIsZeroMemory).
//
// a predicate has only two answers, which makes a careless corpus very easy to pass -- the lesson
// change 259 wrote down. An implementation that always answered "not zero" would agree with the
// shipped export on nearly every random buffer, so this corpus is built to produce both answers in
// quantity and the run REPORTS the split, failing if either side is thin.
//
// And it is built to put the first non-zero byte everywhere, not merely somewhere. The shipped
// export stops at the first non-zero byte, and so does this implementation: a corpus whose
// non-zero byte was always near the front would never reach the 128-byte block loop, the 32-byte
// remainder loop, or the overlapping final vector, while looking thorough.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its three passes and reported 14285 differences with its counter at ZERO -- the
// shipped export disagreeing with itself.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is not used by the loader or heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_iszero_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef BOOLEAN (NTAPI *F_IsZero)(const void*, SIZE_T);

extern BOOLEAN wia_iszeromemory(const void*, SIZE_T);

static volatile LONG c_hit;
static BOOLEAN NTAPI w_iszero(const void* p, SIZE_T n)
{ _InterlockedIncrement(&c_hit); return wia_iszeromemory(p, n); }

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

#define NCASE 40000
#define CAP   20000
static unsigned char buf[CAP];
static SIZE_T cur_n;
static long   cur_pos;                       /* -1 = stays all zero */

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    rs = 0x452821E638D01377ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    /* every rung of the ladder and both loops get their share, not just the sizes a uniform
       distribution would land on */
    cur_n = ((i % 5) == 0) ? (SIZE_T)(rnd() % 40) : (SIZE_T)(rnd() % CAP);
    memset(buf, 0, CAP);
    if ((i % 3) == 0 || cur_n == 0) cur_pos = -1;                  /* all zero */
    else {
        /* the position is drawn across the whole range, so the front, the block loop, the
           remainder and the very last byte all come up */
        cur_pos = (long)(rnd() % (unsigned)cur_n);
        if ((i % 11) == 0) cur_pos = (long)cur_n - 1;              /* ... and the last byte often */
        buf[cur_pos] = (unsigned char)(1 + (rnd() % 255));
    }
}

static unsigned char exp_r[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_IsZero live = (F_IsZero)GetProcAddress(h, "RtlIsZeroMemory");
    patch_t p;
    long i, t = 0, f = 0, last = 0, small = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlIsZeroMemory (change 266) ==\n");

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        exp_r[i] = (unsigned char)(live(buf, cur_n) ? 1 : 0);
        if (exp_r[i]) ++t; else ++f;
        if (cur_pos >= 0 && (SIZE_T)cur_pos == cur_n - 1) ++last;
        if (cur_n < 32) ++small;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED code;  TRUE %ld, false %ld,\n"
           "               with the non-zero byte at the VERY LAST position %ld, and %ld cases\n"
           "               shorter than 32 bytes (the ladder rather than the vector loops)\n",
           NCASE, t, f, last, small);
    OK(t > 5000,    "the corpus rarely answered TRUE");
    OK(f > 5000,    "the corpus rarely answered false");
    OK(last > 500,  "the corpus rarely put the byte at the very end -- the tail would be untested");
    OK(small > 2000,"the corpus rarely went below 32 bytes -- the ladder would be untested");

    {
        long differ = 0;
        if (!patch_on(&p, (void*)live, (void*)w_iszero)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            if ((unsigned char)(live(buf, cur_n) ? 1 : 0) != exp_r[i]) ++differ;
        }
        printf("  [patched]    %d cases, %ld differ;  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "RtlIsZeroMemory answered differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");
        OK(patch_off(&p), "the prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            if ((unsigned char)(live(buf, cur_n) ? 1 : 0) != exp_r[i]) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (both answers in quantity, the non-zero byte at\n"
                      "every position including the last, proved by its own counter and restored\n"
                      "byte-exact)\n", failures);
    return failures ? 1 : 0;
}
