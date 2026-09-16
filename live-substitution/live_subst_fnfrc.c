// live-substitution/live_subst_fnfrc.c
// LIVE-RUN PROOF for change 261 (ntdll!RtlFindNextForwardRunClear and ntdll!RtlFindLastBackwardRunClear).
//
// The two exports are patched ONE AT A TIME, each driven through its own name with its own counter.
// They are separate code in ntdll and they scan in opposite directions, so a wrapper routing one
// through the other would give the WRONG ANSWER rather than merely going unnoticed -- the forward
// form clips the run at FromIndex and the backward form clips it at the other end.
//
// BOTH THE RETURN VALUE AND THE WRITTEN START ARE COMPARED, and the start is poisoned before every
// call. "Nothing found" still writes that pointer, and the two forms write DIFFERENT values there
// -- SizeOfBitMap forward, zero backward -- so an implementation that returned the right length and
// wrote the wrong start would pass a harness that only looked at the return value.
//
// THE CORPUS MUST ACTUALLY FIND RUNS, and the run counts are printed: a corpus of all-ones bitmaps
// would agree with anything, because every call would take the not-found path and never touch the
// scan at all. Both directions are counted separately.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG
// state across its three passes and reported 14285 differences with its counter at ZERO -- the
// shipped export disagreeing with itself -- and that is the discipline this avoids.
//
// THE BITMAP IS ALWAYS AN EVEN NUMBER OF ULONGs, and that is a property of the SHIPPED export
// rather than a convenience. RtlFindLastBackwardRunClear tests its starting bit with
//
//     000F65FF  bt qword ptr [r9], rax
//
// a SIXTY-FOUR-BIT read of the buffer base, so it reads eight bytes whatever the array holds. The
// correctness gate found that with a guard page: at an odd ULONG count the last four of those bytes
// are past the array, and ntdll faults. Here the buffer is a fixed even-sized static array, so the
// read is always inside it -- but the bitmap is still DECLARED at sizes that are not multiples of
// 32 bits, which is what exercises the slack handling.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE exports BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and neither export is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_fnfrc_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Run)(RBM*, ULONG, PULONG);

extern ULONG wia_findnextforwardrunclear(void*, ULONG, PULONG);
extern ULONG wia_findlastbackwardrunclear(void*, ULONG, PULONG);

static volatile LONG c_hit;
static ULONG NTAPI w_fwd(RBM* b, ULONG f, PULONG s)
{ _InterlockedIncrement(&c_hit); return wia_findnextforwardrunclear(b, f, s); }
static ULONG NTAPI w_back(RBM* b, ULONG f, PULONG s)
{ _InterlockedIncrement(&c_hit); return wia_findlastbackwardrunclear(b, f, s); }

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
#define WORDS 512                               /* 16 Kbit, an EVEN number of ULONGs */
static ULONG buf[WORDS];
static ULONG cur_size, cur_from;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int shape = (int)(i % 6), k;
    rs = 0x452821E638D01377ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    /* The shapes are chosen so that the SCAN is what gets tested, not the refusals: mostly-set
       bitmaps with a distant hole are the case the vector skip exists for, and all-ones is the
       case that runs the skip to the very end and finds nothing. */
    for (k = 0; k < WORDS; ++k)
        buf[k] = (shape == 0) ? 0xFFFFFFFFu                         /* nothing to find at all */
               : (shape == 1) ? 0u                                  /* one run over everything */
               : (shape == 2) ? 0xFFFFFFFFu                         /* one hole, planted below */
               : (shape == 3) ? ((k & 1) ? 0xFFFFFFFFu : 0u)        /* alternating whole words */
               : (shape == 4) ? (ULONG)(rnd() | (rnd() << 1))       /* sparse holes */
                              : (ULONG)rnd();                      /* dense holes */
    if (shape == 2) {                            /* ONE run, at a random distance and length */
        ULONG at = rnd() % (WORDS * 32 - 200), len = 1 + rnd() % 190;
        for (k = 0; k < (int)len; ++k) buf[(at + k) >> 5] &= ~(1u << ((at + k) & 31));
    }
    cur_size = 1 + (rnd() % (WORDS * 32));       /* declared at every size, multiple of 32 or not */
    cur_from = rnd() % (cur_size + 16);          /* including at and past the end */
}

static ULONG exp_lf[NCASE], exp_sf[NCASE], exp_lb[NCASE], exp_sb[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Run l_fwd  = (F_Run)GetProcAddress(h, "RtlFindNextForwardRunClear");
    F_Run l_back = (F_Run)GetProcAddress(h, "RtlFindLastBackwardRunClear");
    patch_t p;
    long i, found_f = 0, found_b = 0;
    RBM bm;

    if (!l_fwd || !l_back) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlFindNextForwardRunClear / RtlFindLastBackwardRunClear "
           "(change 261) ==\n");
    bm.Buffer = buf;

    for (i = 0; i < NCASE; ++i) {
        ULONG s = 0xDEADBEEFu;
        build_case(i);
        bm.SizeOfBitMap = cur_size;
        exp_lf[i] = l_fwd(&bm, cur_from, &s);   exp_sf[i] = s;   s = 0xDEADBEEFu;
        exp_lb[i] = l_back(&bm, cur_from, &s);  exp_sb[i] = s;
        if (exp_lf[i]) ++found_f;
        if (exp_lb[i]) ++found_b;
    }
    printf("  [pre-patch]  %d cases x 2 exports recorded from the SHIPPED code;  a run was FOUND "
           "%ld times forward, %ld backward\n", NCASE, found_f, found_b);
    OK(found_f > 1000 && found_b > 1000,
       "the corpus barely ever found a run -- it would have tested only the not-found paths");

#define ONE(NAME, LIVE, REPL, EXPL, EXPS)                                                     \
    do {                                                                                      \
        long differ = 0;                                                                      \
        if (!patch_on(&p, (void*)(LIVE), (void*)(REPL))) { printf("  FAIL: patch\n"); return 1; } \
        c_hit = 0;                                                                            \
        for (i = 0; i < NCASE; ++i) {                                                         \
            ULONG s = 0xDEADBEEFu, l;                                                         \
            build_case(i);                                                                    \
            bm.SizeOfBitMap = cur_size;                                                       \
            l = (LIVE)(&bm, cur_from, &s);                                                    \
            if (l != (EXPL)[i] || s != (EXPS)[i]) ++differ;                                   \
        }                                                                                     \
        printf("  [%-27s] %d cases, %ld differ;  our-code calls = %ld\n",                     \
               NAME, NCASE, differ, (long)c_hit);                                             \
        OK(differ == 0, NAME " answered differently under the patch");                        \
        OK(c_hit == (LONG)NCASE, NAME " counter did not move once per call");                 \
        OK(patch_off(&p), NAME " prologue was not restored byte-for-byte");                   \
    } while (0)

    ONE("RtlFindNextForwardRunClear",  l_fwd,  w_fwd,  exp_lf, exp_sf);
    ONE("RtlFindLastBackwardRunClear", l_back, w_back, exp_lb, exp_sb);

    /* ---- and the whole corpus again, through the RESTORED exports ---- */
    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            ULONG s = 0xDEADBEEFu;
            build_case(i);
            bm.SizeOfBitMap = cur_size;
            if (l_fwd(&bm, cur_from, &s) != exp_lf[i] || s != exp_sf[i]) ++post;
            s = 0xDEADBEEFu;
            if (l_back(&bm, cur_from, &s) != exp_lb[i] || s != exp_sb[i]) ++post;
        }
        printf("  [post]       %d cases x 2 through the RESTORED exports, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "a restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (both exports patched separately, length AND "
                      "written start compared, each proved by its own counter, both restored "
                      "byte-exact)\n", failures);
    return failures ? 1 : 0;
}
