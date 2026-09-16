// live-substitution/live_subst_fcr.c
// LIVE-RUN PROOF for change 258 (ntdll!RtlFindClearRuns).
//
// ONE export, but TWO forms, and they are driven as two separate corpora with two separate
// counters. SortByLength is not a flag on a shared path here -- it selects between a 64-bit word
// scan and a byte scan whose EMISSION ORDER is the thing being matched -- so a harness that mixed
// the two could pass while one of them was wrong.
//
// WHAT IS COMPARED IS THE WHOLE ARRAY, not the return value. The unsorted form returns the first
// runs FOUND, and the order they are found in is not left to right (probes/enumorder.c): a byte
// holding a one-bit run at 1 and a two-bit run at 3 reports (3,2) first. A harness that compared
// only the count would pass an implementation that returned the right number of wrong runs, which
// is exactly the bug the correctness corpus caught in this change's first draft.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG
// state across its three passes and reported 14285 differences with its counter at ZERO -- the
// shipped export disagreeing with itself -- and that is the discipline this avoids.
//
// SizeOfRunArray = 0 IS EXCLUDED, and not for convenience: probes/contract.c established that the
// SHIPPED export takes an access violation there once the bitmap has a run to report. There is no
// behaviour to match, so the corpus never generates it.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and this export is used by neither the loader nor
//       the heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_fcr_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef struct { ULONG StartingIndex; ULONG NumberOfBits; } RUN;
typedef ULONG (NTAPI *F_Runs)(RBM*, RUN*, ULONG, BOOLEAN);

extern ULONG wia_findclearruns(void*, RUN*, ULONG, BOOLEAN);

static volatile LONG c_hit;
static ULONG NTAPI w_runs(RBM* b, RUN* a, ULONG cap, BOOLEAN s)
{ _InterlockedIncrement(&c_hit); return wia_findclearruns(b, a, cap, s); }

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
#define WORDS 512
#define CAPMAX 64
static ULONG buf[WORDS];
static RUN   out[CAPMAX + 4];
static ULONG cur_size, cur_cap;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int shape = (int)(i % 6), k;
    rs = 0x61C8864680B583EBull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    for (k = 0; k < WORDS; ++k)
        buf[k] = (shape == 0) ? 0u                             /* one enormous run */
               : (shape == 1) ? 0xFFFFFFFFu                     /* no clear bits at all */
               : (shape == 2) ? 0xA5A5A5A5u                     /* a run every two or three bits */
               : (shape == 3) ? 0x0F0F0F0Fu                     /* four-bit runs */
               : (shape == 4) ? ((k & 1) ? 0xFFFFFFFFu : 0u)    /* uniform over a ULONG, not a pair */
                              : (ULONG)rnd();
    cur_size = 1 + (rnd() % (WORDS * 32));      /* on and off the byte and word boundaries */
    cur_cap  = 1 + (rnd() % CAPMAX);            /* never 0: the shipped export faults there */
}

/* the WHOLE array, entry by entry, folded into one value */
static unsigned long long digest(ULONG n)
{
    unsigned long long h = 0xCBF29CE484222325ull;
    ULONG i;
    h ^= n; h *= 0x100000001B3ull;
    for (i = 0; i < n; ++i) {
        h ^= out[i].StartingIndex; h *= 0x100000001B3ull;
        h ^= out[i].NumberOfBits;  h *= 0x100000001B3ull;
    }
    return h;
}

static unsigned long long exp_s[NCASE], exp_u[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Runs live = (F_Runs)GetProcAddress(h, "RtlFindClearRuns");
    patch_t p;
    long i;
    RBM bm;

    if (!live) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlFindClearRuns (change 258) ==\n");
    printf("   the WHOLE run array is compared, not the count: the unsorted form's ORDER is the\n"
           "   thing being matched, and it is not left to right\n");
    bm.Buffer = buf;

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        bm.SizeOfBitMap = cur_size;
        memset(out, 0xEE, sizeof out);
        exp_s[i] = digest(live(&bm, out, cur_cap, TRUE));
        memset(out, 0xEE, sizeof out);
        exp_u[i] = digest(live(&bm, out, cur_cap, FALSE));
    }
    printf("  [pre-patch]  %d cases x 2 forms recorded from the SHIPPED code\n", NCASE);

#define ONE(NAME, SORTED, EXPECT)                                                             \
    do {                                                                                      \
        long differ = 0;                                                                      \
        if (!patch_on(&p, (void*)live, (void*)w_runs)) { printf("  FAIL: patch\n"); return 1; } \
        c_hit = 0;                                                                            \
        for (i = 0; i < NCASE; ++i) {                                                         \
            build_case(i);                                                                    \
            bm.SizeOfBitMap = cur_size;                                                       \
            memset(out, 0xEE, sizeof out);                                                    \
            if (digest(live(&bm, out, cur_cap, (BOOLEAN)(SORTED))) != (EXPECT)[i]) ++differ;  \
        }                                                                                     \
        printf("  [%-24s] %d cases, %ld differ;  our-code calls = %ld\n",                     \
               NAME, NCASE, differ, (long)c_hit);                                             \
        OK(differ == 0, NAME " answered differently under the patch");                        \
        OK(c_hit == (LONG)NCASE, NAME " counter did not move once per call");                 \
        OK(patch_off(&p), NAME " prologue was not restored byte-for-byte");                   \
    } while (0)

    ONE("SortByLength = TRUE",  TRUE,  exp_s);
    ONE("SortByLength = FALSE", FALSE, exp_u);

    /* ---- and the whole corpus again, through the RESTORED export ---- */
    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            bm.SizeOfBitMap = cur_size;
            memset(out, 0xEE, sizeof out);
            if (digest(live(&bm, out, cur_cap, TRUE))  != exp_s[i]) ++post;
            memset(out, 0xEE, sizeof out);
            if (digest(live(&bm, out, cur_cap, FALSE)) != exp_u[i]) ++post;
        }
        printf("  [post]       %d cases x 2 through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (both forms patched separately, each proved by its "
                      "own counter, prologue restored byte-exact)\n", failures);
    return failures ? 1 : 0;
}
