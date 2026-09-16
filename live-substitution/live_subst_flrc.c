// live-substitution/live_subst_flrc.c
// LIVE-RUN PROOF for change 255 (ntdll!RtlFindLongestRunClear).
//
// The export is hot-patched in a sacrificial child so that every subsequent call BY NAME runs our
// assembly, and BOTH observables are compared -- the returned length AND the written
// *StartingIndex. The index is where the tie-break lives: an implementation that updated its best
// on ">=" instead of ">" would return the right LENGTH on every bitmap and the wrong INDEX only
// when two runs tie, so a harness that checked the length alone would prove very little.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass, which is the discipline change 252's
// harness needed after it carried PRNG state across its three passes and reported 14285 differences
// with its counter at ZERO -- the shipped export disagreeing with itself.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and this routine is used by neither the loader nor
//       the heap.
//   (3) REVERSIBLE: original bytes restored, the restore VERIFIED byte-for-byte, and the whole
//       corpus run again through the restored export.
//
// Build: build_flrc_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Longest)(RBM*, PULONG);

extern ULONG wia_findlongestrunclear(void*, ULONG*);

static volatile LONG c_flrc;
static ULONG NTAPI w_flrc(RBM* bm, PULONG ix)
{
    _InterlockedIncrement(&c_flrc);
    return wia_findlongestrunclear(bm, ix);
}

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;

static void raw_copy(volatile unsigned char* dst, const volatile unsigned char* src, int n)
{
    int i;
    for (i = 0; i < n; ++i) dst[i] = src[i];
}
static int patch_on(patch_t* p, void* target, void* repl)
{
    DWORD old;
    unsigned char stub[14];
    p->target = target; p->on = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    stub[0] = 0xFF; stub[1] = 0x25;
    *(uint32_t*)(stub + 2) = 0;
    *(uint64_t*)(stub + 6) = (uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p)
{
    DWORD old;
    int i;
    if (!p->on) return 1;
    VirtualProtect(p->target, 16, PAGE_EXECUTE_READWRITE, &old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for (i = 0; i < 16; ++i)
        if (((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

static int failures = 0;
#define OK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", (msg)); ++failures; } } while (0)

#define NCASE 40000
#define WORDS 256
static ULONG buf[WORDS];
static ULONG cur_size;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int shape = (int)(i % 7), k, b;
    rs = 0x2545F4914F6CDD1Dull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    for (k = 0; k < WORDS; ++k)
        buf[k] = (shape == 0) ? 0xFFFFFFFFu                       /* all set */
               : (shape == 1) ? 0u                                /* all clear */
               : (shape == 2) ? 0xA5A5A5A5u                       /* a run every two bits */
               : (shape == 3) ? ((k & 1) ? 0xFFFFFFFFu : 0u)      /* half and half */
               : (shape == 4) ? 0xFFFFFFFFu                       /* dense, holes punched below */
               : (shape == 5) ? rnd()                             /* random */
                              : 0xFFFFFFFFu;                      /* ties, punched below */
    if (shape == 4)
        for (k = 0; k < 20; ++k) {
            ULONG st = rnd() % (WORDS * 32 - 64), ln = 1 + (rnd() % 50), x;
            for (x = st; x < st + ln && x < WORDS * 32; ++x) buf[x >> 5] &= ~(1u << (x & 31));
        }
    if (shape == 6) {
        /* MANY EQUAL RUNS, so the first must win -- the case the index exists to test */
        ULONG len = 1 + (rnd() % 9), gap = 1 + (rnd() % 9), st = rnd() % 64;
        for (b = (int)st; b + (int)len <= WORDS * 32; b += (int)(len + gap))
            for (k = 0; k < (int)len; ++k) buf[(b + k) >> 5] &= ~(1u << ((b + k) & 31));
    }
    cur_size = 1 + (rnd() % (WORDS * 32));
}

static ULONG exp_r[NCASE];
static ULONG exp_i[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Longest live = (F_Longest)GetProcAddress(h, "RtlFindLongestRunClear");
    patch_t p;
    long i, differ = 0;
    RBM bm;

    if (!live) { printf("RtlFindLongestRunClear not found\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlFindLongestRunClear (change 255) ==\n");
    printf("  export at %p\n", (void*)live);

    bm.Buffer = buf;

    /* ---- (1) VALIDATE FIRST ---- */
    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        bm.SizeOfBitMap = cur_size;
        exp_i[i] = 0xCCCCCCCCu;
        exp_r[i] = live(&bm, &exp_i[i]);
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export (length AND start index)\n",
           NCASE);

    /* ---- (2) PATCH ---- */
    if (!patch_on(&p, (void*)live, (void*)w_flrc)) { printf("  FAIL: could not patch\n"); return 1; }
    printf("  [patched]    export redirected to our assembly\n");

    /* ---- (3) the same corpus, through the EXPORT BY NAME ---- */
    c_flrc = 0;
    for (i = 0; i < NCASE; ++i) {
        ULONG r, ix = 0xCCCCCCCCu;
        build_case(i);
        bm.SizeOfBitMap = cur_size;
        r = live(&bm, &ix);
        if (r != exp_r[i] || ix != exp_i[i]) {
            if (differ < 10)
                printf("  DIFFER case %ld: shipped=(%lu,%lu) ours=(%lu,%lu) size=%lu\n",
                       i, exp_r[i], exp_i[i], r, ix, cur_size);
            ++differ;
        }
    }
    printf("  [patched]    %d cases, %ld differ;  our-code calls through the export = %ld\n",
           NCASE, differ, (long)c_flrc);
    OK(differ == 0, "a length or start index through the patched export differs");
    OK(c_flrc == (LONG)NCASE, "the counter did not move once per call");

    /* ---- (4) RESTORE and verify ---- */
    OK(patch_off(&p), "the restored prologue is NOT byte-identical to the original");
    printf("  [restored]   prologue verified byte-for-byte\n");
    {
        long post = 0;
        LONG before = c_flrc;
        for (i = 0; i < NCASE; ++i) {
            ULONG r, ix = 0xCCCCCCCCu;
            build_case(i);
            bm.SizeOfBitMap = cur_size;
            r = live(&bm, &ix);
            if (r != exp_r[i] || ix != exp_i[i]) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  our-code calls "
               "= %ld (must not have moved)\n", NCASE, post, (long)(c_flrc - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_flrc == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (patched, proved by counter, restored byte-exact)\n",
           failures);
    return failures ? 1 : 0;
}
