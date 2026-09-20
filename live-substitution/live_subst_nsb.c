// live-substitution/live_subst_nsb.c
// LIVE-RUN PROOF for change 257 (ntdll!RtlNumberOfSetBits and its three relatives).
//
// All FOUR exports are patched, one at a time, and each is driven through its own name with its
// own counter. They are patched separately rather than together because they are separate code in
// ntdll and a wrapper that happened to route one through another would otherwise go unnoticed --
// the same reason changes 249 and 250 patched each half of their pair on its own.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its three passes and reported 14285 differences with its counter at ZERO -- the
// shipped export disagreeing with itself -- and that is the discipline this avoids.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live exports before any patch exists.
//   (2) Patch only when idle: single-threaded, and none of these is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_nsb_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Count)(RBM*);
typedef ULONG (NTAPI *F_Range)(RBM*, ULONG, ULONG);

extern ULONG wia_numberofsetbits(void*);
extern ULONG wia_numberofclearbits(void*);
extern ULONG wia_numberofsetbitsinrange(void*, ULONG, ULONG);
extern ULONG wia_numberofclearbitsinrange(void*, ULONG, ULONG);

static volatile LONG c_hit;
static ULONG NTAPI w_set(RBM* b)                        { _InterlockedIncrement(&c_hit); return wia_numberofsetbits(b); }
static ULONG NTAPI w_clr(RBM* b)                        { _InterlockedIncrement(&c_hit); return wia_numberofclearbits(b); }
static ULONG NTAPI w_rset(RBM* b, ULONG s, ULONG l)     { _InterlockedIncrement(&c_hit); return wia_numberofsetbitsinrange(b, s, l); }
static ULONG NTAPI w_rclr(RBM* b, ULONG s, ULONG l)     { _InterlockedIncrement(&c_hit); return wia_numberofclearbitsinrange(b, s, l); }

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
static ULONG buf[WORDS];
static ULONG cur_size, cur_st, cur_ln;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int shape = (int)(i % 5), k;
    rs = 0x61C8864680B583EBull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    for (k = 0; k < WORDS; ++k)
        buf[k] = (shape == 0) ? 0u : (shape == 1) ? 0xFFFFFFFFu
               : (shape == 2) ? 0xA5A5A5A5u : (shape == 3) ? 0x0F0F0F0Fu : (ULONG)rnd();
    /* sizes spanning the single-word fast path, the vector body and everything between */
    cur_size = 1 + (rnd() % (WORDS * 32));
    cur_st   = rnd() % (cur_size + 4);
    cur_ln   = rnd() % (cur_size + 4);
}

static ULONG exp_a[NCASE], exp_b[NCASE], exp_c[NCASE], exp_d[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Count l_set = (F_Count)GetProcAddress(h, "RtlNumberOfSetBits");
    F_Count l_clr = (F_Count)GetProcAddress(h, "RtlNumberOfClearBits");
    F_Range l_rs  = (F_Range)GetProcAddress(h, "RtlNumberOfSetBitsInRange");
    F_Range l_rc  = (F_Range)GetProcAddress(h, "RtlNumberOfClearBitsInRange");
    patch_t p;
    long i;
    RBM bm;

    if (!l_set || !l_clr || !l_rs || !l_rc) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlNumberOfSetBits family (change 257) ==\n");
    bm.Buffer = buf;

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        bm.SizeOfBitMap = cur_size;
        exp_a[i] = l_set(&bm);
        exp_b[i] = l_clr(&bm);
        exp_c[i] = l_rs(&bm, cur_st, cur_ln);
        exp_d[i] = l_rc(&bm, cur_st, cur_ln);
    }
    printf("  [pre-patch]  %d cases x 4 exports recorded from the SHIPPED code\n", NCASE);

#define ONE(NAME, LIVE, REPL, EXPECT, CALL)                                                   \
    do {                                                                                      \
        long differ = 0;                                                                      \
        if (!patch_on(&p, (void*)(LIVE), (void*)(REPL))) { printf("  FAIL: patch\n"); return 1; } \
        c_hit = 0;                                                                            \
        for (i = 0; i < NCASE; ++i) {                                                         \
            ULONG r;                                                                          \
            build_case(i);                                                                    \
            bm.SizeOfBitMap = cur_size;                                                       \
            r = (CALL);                                                                       \
            if (r != (EXPECT)[i]) ++differ;                                                   \
        }                                                                                     \
        printf("  [%-28s] %d cases, %ld differ;  our-code calls = %ld\n",                     \
               NAME, NCASE, differ, (long)c_hit);                                             \
        OK(differ == 0, NAME " answered differently under the patch");                        \
        OK(c_hit == (LONG)NCASE, NAME " counter did not move once per call");                 \
        OK(patch_off(&p), NAME " prologue was not restored byte-for-byte");                   \
    } while (0)

    ONE("RtlNumberOfSetBits",         l_set, w_set,  exp_a, l_set(&bm));
    ONE("RtlNumberOfClearBits",       l_clr, w_clr,  exp_b, l_clr(&bm));
    ONE("RtlNumberOfSetBitsInRange",  l_rs,  w_rset, exp_c, l_rs(&bm, cur_st, cur_ln));
    ONE("RtlNumberOfClearBitsInRange",l_rc,  w_rclr, exp_d, l_rc(&bm, cur_st, cur_ln));

    /* ---- and the whole corpus again, through the RESTORED exports ---- */
    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            bm.SizeOfBitMap = cur_size;
            if (l_set(&bm) != exp_a[i]) ++post;
            if (l_clr(&bm) != exp_b[i]) ++post;
            if (l_rs(&bm, cur_st, cur_ln) != exp_c[i]) ++post;
            if (l_rc(&bm, cur_st, cur_ln) != exp_d[i]) ++post;
        }
        printf("  [post]       %d cases x 4 through the RESTORED exports, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "a restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (four exports patched separately, each proved by "
                      "its own counter, all restored byte-exact)\n", failures);
    return failures ? 1 : 0;
}
