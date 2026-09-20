// live-substitution/live_subst_fsbc.c
// LIVE-RUN PROOF for change 262 (ntdll!RtlFindSetBitsAndClear and ntdll!RtlFindClearBitsAndSet).
//
// The two exports are patched one at a time, each driven through its own name with its own counter.
//
// What is compared is the answer *and* the bitmap the call left behind. These functions mutate, and
// the mutation is the half this change adds, so a run that only checked return values would be
// testing change 256 and calling it change 262. The whole buffer is folded into a 64-bit FNV-1a
// hash after every call and the hash is compared; two different 2 KB buffers colliding on 64 bits
// is not a risk worth engineering around, and a difference of even one bit changes it.
//
// Every case rebuilds its bitmap from the case index, and here that is not merely hygiene the way
// it was for the read-only changes; it is the only way the corpus means anything. A call CONSUMES
// what it finds: run the same case twice on one buffer and the second call is a different question
// from the first. Change 252's harness carried PRNG state across its passes and reported 14285
// differences with its counter at ZERO (the shipped export disagreeing with itself) and a
// mutating export would produce that failure from a single missed reset.
//
// The three arms are counted and the run fails if any is empty: found-and-wrote, not-found, and
// NumberToFind = 0. Only the first writes anything at all, so a corpus that never found a run would
// have proved nothing about the mutation, which is the entire subject.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll, never a live system process, never the file on disk.
//   (1) Validate first against the live exports before any patch exists.
//   (2) Patch only when idle: single-threaded, and neither export is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_fsbc_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

extern ULONG wia_findsetbitsandclear(void*, ULONG, ULONG);
extern ULONG wia_findclearbitsandset(void*, ULONG, ULONG);

static volatile LONG c_hit;
static ULONG NTAPI w_fsac(RBM* b, ULONG n, ULONG h)
{ _InterlockedIncrement(&c_hit); return wia_findsetbitsandclear(b, n, h); }
static ULONG NTAPI w_fcas(RBM* b, ULONG n, ULONG h)
{ _InterlockedIncrement(&c_hit); return wia_findclearbitsandset(b, n, h); }

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

#define NCASE 30000
#define WORDS 512                               /* 16 Kbit */
static ULONG buf[WORDS];
static ULONG cur_size, cur_n, cur_hint;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int shape = (int)(i % 6), k;
    rs = 0x452821E638D01377ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    for (k = 0; k < WORDS; ++k)
        buf[k] = (shape == 0) ? 0xFFFFFFFFu                      /* every set run there is */
               : (shape == 1) ? 0u                               /* every clear run there is */
               : (shape == 2) ? 0xA5A5A5A5u                      /* no run of anything: a full scan */
               : (shape == 3) ? ((k & 1) ? 0xFFFFFFFFu : 0u)     /* whole alternating words */
               : (shape == 4) ? (ULONG)(rnd() | rnd())
                              : (ULONG)rnd();
    cur_size = 1 + (rnd() % (WORDS * 32));
    /* small sizes on purpose: 64 bits or fewer is a completely different path in our code */
    if ((rnd() & 7) == 0) cur_size = 1 + (rnd() % 70);
    /* N = 0 Is asked for deliberately, one case in sixteen. Leaving it to a range that happens to
       include zero produced 33 cases in 30000 -- the census caught that, which is what a census is
       for. N = 0 is one of the two paths that must write nothing at all, and it is the one an
       implementation is most likely to get wrong by mutating "zero bits" through a loop that runs
       at least once. */
    cur_n    = ((rnd() & 15) == 0) ? 0
             : (rnd() & 3) ? 1 + (rnd() % 40) : 1 + (rnd() % 200);
    cur_hint = rnd() % (cur_size + 32);                          /* including at and past the end */
}

static uint64_t hash_buf(ULONG nw)
{
    uint64_t h = 1469598103934665603ull;
    ULONG i;
    for (i = 0; i < nw; ++i) {
        int b;
        for (b = 0; b < 4; ++b) {
            h ^= (unsigned char)(buf[i] >> (b * 8));
            h *= 1099511628211ull;
        }
    }
    return h;
}

static ULONG   exp_r[NCASE];
static uint64_t exp_h[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Find l_fsac = (F_Find)GetProcAddress(h, "RtlFindSetBitsAndClear");
    F_Find l_fcas = (F_Find)GetProcAddress(h, "RtlFindClearBitsAndSet");
    patch_t p;
    long i, found = 0, none = 0, zero = 0;
    RBM bm;

    if (!l_fsac || !l_fcas) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlFindSetBitsAndClear / RtlFindClearBitsAndSet "
           "(change 262) ==\n");
    bm.Buffer = buf;

#define PASS(LIVE, WHICH)                                                                     \
    do {                                                                                      \
        for (i = 0; i < NCASE; ++i) {                                                         \
            ULONG r;                                                                          \
            build_case(i);                                                                    \
            bm.SizeOfBitMap = cur_size;                                                       \
            r = (LIVE)(&bm, cur_n, cur_hint);                                                 \
            exp_r[i] = r;                                                                     \
            exp_h[i] = hash_buf((cur_size + 31) / 32);                                        \
            if (cur_n == 0) ++zero; else if (r == 0xFFFFFFFFul) ++none; else ++found;          \
        }                                                                                     \
        printf("  [pre-patch %-12s] %d cases recorded from the SHIPPED code;  "                \
               "found-and-wrote %ld, not-found %ld, N=0 %ld\n", WHICH, NCASE, found, none, zero); \
        OK(found > 1000, WHICH " never found a run -- the mutation would be untested");        \
        OK(none  > 100,  WHICH " never missed -- the no-write path would be untested");        \
        OK(zero  > 100,  WHICH " never saw N=0 -- the other no-write path would be untested"); \
    } while (0)

#define ONE(NAME, LIVE, REPL)                                                                 \
    do {                                                                                      \
        long differ = 0;                                                                      \
        if (!patch_on(&p, (void*)(LIVE), (void*)(REPL))) { printf("  FAIL: patch\n"); return 1; } \
        c_hit = 0;                                                                            \
        for (i = 0; i < NCASE; ++i) {                                                         \
            ULONG r;                                                                          \
            build_case(i);                                                                    \
            bm.SizeOfBitMap = cur_size;                                                       \
            r = (LIVE)(&bm, cur_n, cur_hint);                                                 \
            if (r != exp_r[i] || hash_buf((cur_size + 31) / 32) != exp_h[i]) ++differ;         \
        }                                                                                     \
        printf("  [%-24s] %d cases, %ld differ (answer AND the whole bitmap);  "               \
               "our-code calls = %ld\n", NAME, NCASE, differ, (long)c_hit);                    \
        OK(differ == 0, NAME " answered or wrote differently under the patch");                \
        OK(c_hit == (LONG)NCASE, NAME " counter did not move once per call");                  \
        OK(patch_off(&p), NAME " prologue was not restored byte-for-byte");                    \
    } while (0)

    found = none = zero = 0;
    PASS(l_fsac, "SetBits&Clear");
    ONE("RtlFindSetBitsAndClear", l_fsac, w_fsac);
    {   /* and the corpus again through the RESTORED export */
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            ULONG r;
            build_case(i);
            bm.SizeOfBitMap = cur_size;
            r = l_fsac(&bm, cur_n, cur_hint);
            if (r != exp_r[i] || hash_buf((cur_size + 31) / 32) != exp_h[i]) ++post;
        }
        printf("  [post  SetBits&Clear    ] %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored RtlFindSetBitsAndClear no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    found = none = zero = 0;
    PASS(l_fcas, "ClearBits&Set");
    ONE("RtlFindClearBitsAndSet", l_fcas, w_fcas);
    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            ULONG r;
            build_case(i);
            bm.SizeOfBitMap = cur_size;
            r = l_fcas(&bm, cur_n, cur_hint);
            if (r != exp_r[i] || hash_buf((cur_size + 31) / 32) != exp_h[i]) ++post;
        }
        printf("  [post  ClearBits&Set    ] %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored RtlFindClearBitsAndSet no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (both exports patched separately, ANSWER AND "
                      "RESULTING BITMAP compared, each proved by its own counter, both restored "
                      "byte-exact)\n", failures);
    return failures ? 1 : 0;
}
