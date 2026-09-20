// live-substitution/live_subst_areb.c
// LIVE-RUN PROOF for change 259 (ntdll!RtlAreBitsSet and ntdll!RtlAreBitsClear).
//
// The two exports are patched one at a time, each driven through its own name with its own counter.
// They are separate code in ntdll and one implementation serves both here, so a wrapper routing one
// through the other would otherwise go unnoticed, the same reason changes 249 and 250 patched each
// half of their pair alone.
//
// a predicate has only two answers, which makes a careless corpus very easy to pass: an
// implementation that always said NO would agree with the live export on nearly every random range
// over a random bitmap, because almost none of them is uniform. So the corpus is built to produce
// both answers and the run reports how many of each it got; a pass with no YES at all would have
// proved nothing about the loop, only about the refusals.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its three passes and reported 14285 differences with its counter at ZERO, the
// shipped export disagreeing with itself, and that is the discipline this avoids.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll, never a live system process, never the file on disk.
//   (1) Validate first against the live exports before any patch exists.
//   (2) Patch only when idle: single-threaded, and neither export is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_areb_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef BOOLEAN (NTAPI *F_Are)(RBM*, ULONG, ULONG);

extern BOOLEAN wia_arebitsset(void*, ULONG, ULONG);
extern BOOLEAN wia_arebitsclear(void*, ULONG, ULONG);

static volatile LONG c_hit;
static BOOLEAN NTAPI w_set(RBM* b, ULONG s, ULONG l) { _InterlockedIncrement(&c_hit); return wia_arebitsset(b, s, l); }
static BOOLEAN NTAPI w_clr(RBM* b, ULONG s, ULONG l) { _InterlockedIncrement(&c_hit); return wia_arebitsclear(b, s, l); }

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
static ULONG cur_size, cur_start, cur_len;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int shape = (int)(i % 5), k;
    rs = 0x452821E638D01377ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    /* Three of the five shapes are uniform, so the answer is yes for most ranges inside them --
       a corpus of random bitmaps alone would answer NO almost every time and test nothing. */
    for (k = 0; k < WORDS; ++k)
        buf[k] = (shape == 0) ? 0xFFFFFFFFu
               : (shape == 1) ? 0u
               : (shape == 2) ? 0xFFFFFFFFu
               : (shape == 3) ? ((k & 1) ? 0xFFFFFFFFu : 0u)
                              : (ULONG)rnd();
    if (shape == 2) {                            /* uniform except for ONE wrong bit */
        ULONG bad = rnd() % (WORDS * 32);
        buf[bad >> 5] &= ~(1u << (bad & 31));
    }
    cur_size  = 1 + (rnd() % (WORDS * 32));
    cur_start = rnd() % (cur_size + 16);         /* including starts at and past the end */
    if (rnd() & 1) cur_len = rnd() % 70;         /* short: one and two masked words */
    else           cur_len = rnd() % (cur_size + 16);
}

static unsigned char exp_s[NCASE], exp_c[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Are l_set = (F_Are)GetProcAddress(h, "RtlAreBitsSet");
    F_Are l_clr = (F_Are)GetProcAddress(h, "RtlAreBitsClear");
    patch_t p;
    long i, yes_s = 0, yes_c = 0;
    RBM bm;

    if (!l_set || !l_clr) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlAreBitsSet / RtlAreBitsClear (change 259) ==\n");
    bm.Buffer = buf;

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        bm.SizeOfBitMap = cur_size;
        exp_s[i] = (unsigned char)(l_set(&bm, cur_start, cur_len) ? 1 : 0);
        exp_c[i] = (unsigned char)(l_clr(&bm, cur_start, cur_len) ? 1 : 0);
        yes_s += exp_s[i]; yes_c += exp_c[i];
    }
    printf("  [pre-patch]  %d cases x 2 exports recorded from the SHIPPED code;  YES %ld / %ld\n",
           NCASE, yes_s, yes_c);
    OK(yes_s > 1000 && yes_c > 1000, "the corpus barely ever answered YES -- it would prove nothing");

#define ONE(NAME, LIVE, REPL, EXPECT)                                                         \
    do {                                                                                      \
        long differ = 0;                                                                      \
        if (!patch_on(&p, (void*)(LIVE), (void*)(REPL))) { printf("  FAIL: patch\n"); return 1; } \
        c_hit = 0;                                                                            \
        for (i = 0; i < NCASE; ++i) {                                                         \
            build_case(i);                                                                    \
            bm.SizeOfBitMap = cur_size;                                                       \
            if ((unsigned char)((LIVE)(&bm, cur_start, cur_len) ? 1 : 0) != (EXPECT)[i])      \
                ++differ;                                                                     \
        }                                                                                     \
        printf("  [%-16s] %d cases, %ld differ;  our-code calls = %ld\n",                     \
               NAME, NCASE, differ, (long)c_hit);                                             \
        OK(differ == 0, NAME " answered differently under the patch");                        \
        OK(c_hit == (LONG)NCASE, NAME " counter did not move once per call");                 \
        OK(patch_off(&p), NAME " prologue was not restored byte-for-byte");                   \
    } while (0)

    ONE("RtlAreBitsSet",   l_set, w_set, exp_s);
    ONE("RtlAreBitsClear", l_clr, w_clr, exp_c);

    /* ---- and the whole corpus again, through the RESTORED exports ---- */
    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            bm.SizeOfBitMap = cur_size;
            if ((unsigned char)(l_set(&bm, cur_start, cur_len) ? 1 : 0) != exp_s[i]) ++post;
            if ((unsigned char)(l_clr(&bm, cur_start, cur_len) ? 1 : 0) != exp_c[i]) ++post;
        }
        printf("  [post]       %d cases x 2 through the RESTORED exports, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "a restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (both exports patched separately, each proved by "
                      "its own counter, both restored byte-exact)\n", failures);
    return failures ? 1 : 0;
}
