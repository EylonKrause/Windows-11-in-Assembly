// live-substitution/live_subst_fsb.c
// LIVE-RUN PROOF for change 256 (ntdll!RtlFindSetBits and ntdll!RtlFindClearBits).
//
// The two exports are patched ONE AT A TIME, each driven through its own name with its own counter.
// They are separate code in ntdll -- 0x111210 and 0x0D0140 -- and one implementation serves both
// here, so a wrapper routing one through the other would otherwise go unnoticed; that is the same
// reason changes 249 and 250 patched each half of their pair alone.
//
// EVERY CASE SWEEPS THE HINT, because the search WRAPS: it scans [hint, size) and then starts again
// from the beginning, and a run straddling the wrap point does not count. On any bitmap whose
// answer lies after the hint -- which is most bitmaps -- an implementation built on "search forward
// and stop" is indistinguishable from a correct one, so a corpus that left the hint at zero would
// prove nothing about the half of the contract that is hardest to get right.
//
// AND N IS SWEPT ACROSS EVERY WITNESS BLOCK SIZE, since N is what picks it: pairs (3..6), nibbles
// (7..14), bytes (15..30), words (31..62), dwords (63..126) and qwords (127 and up), plus N below
// three, which has no useful block and runs the scalar scanner, and N = 0, which returns the hint
// rounded down to a multiple of eight and nothing else.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG
// state across its three passes and reported 14285 differences with its counter at ZERO -- the
// shipped export disagreeing with itself -- and that is the discipline this avoids.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE exports BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and neither export is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_fsb_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

extern ULONG wia_findsetbits(void*, ULONG, ULONG);
extern ULONG wia_findclearbits(void*, ULONG, ULONG);

static volatile LONG c_hit;
static ULONG NTAPI w_set(RBM* b, ULONG n, ULONG h) { _InterlockedIncrement(&c_hit); return wia_findsetbits(b, n, h); }
static ULONG NTAPI w_clr(RBM* b, ULONG n, ULONG h) { _InterlockedIncrement(&c_hit); return wia_findclearbits(b, n, h); }

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
    int shape = (int)(i % 7), k;
    rs = 0x61C8864680B583EBull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    for (k = 0; k < WORDS; ++k)
        buf[k] = (shape == 0) ? 0u                            /* nothing set at all */
               : (shape == 1) ? 0xFFFFFFFFu                    /* one run over everything */
               : (shape == 2) ? 0xA5A5A5A5u                    /* top bit SET: the survey's subject */
               : (shape == 3) ? 0x5A5A5A5Au                    /* top bit CLEAR: the mirror of it */
               : (shape == 4) ? ((k & 1) ? 0xFFFFFFFFu : 0u)   /* uniform over a ULONG, not a pair */
               : (shape == 5) ? (ULONG)((rnd() & 7) ? 0u : 0xFFFFFFFFu)  /* scattered whole words */
                              : (ULONG)rnd();
    if (shape == 6) {                                          /* plus a few real runs */
        int j;
        for (j = 0; j < 20; ++j) {
            ULONG s = rnd() % 16000, L = 1 + (rnd() % 400), x;
            for (x = s; x < s + L && x < WORDS * 32; ++x) buf[x >> 5] |= (1u << (x & 31));
        }
    }
    cur_size = 1 + (rnd() % (WORDS * 32));
    cur_n    = rnd() % 520;                      /* 0 and every block class, up to beyond the map */
    cur_hint = rnd() % (cur_size + 64);          /* including hints at and past the end */
}

static ULONG exp_s[NCASE], exp_c[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Find l_set = (F_Find)GetProcAddress(h, "RtlFindSetBits");
    F_Find l_clr = (F_Find)GetProcAddress(h, "RtlFindClearBits");
    patch_t p;
    long i;
    RBM bm;

    if (!l_set || !l_clr) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlFindSetBits / RtlFindClearBits (change 256) ==\n");
    bm.Buffer = buf;

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        bm.SizeOfBitMap = cur_size;
        exp_s[i] = l_set(&bm, cur_n, cur_hint);
        exp_c[i] = l_clr(&bm, cur_n, cur_hint);
    }
    printf("  [pre-patch]  %d cases x 2 exports recorded from the SHIPPED code\n", NCASE);

#define ONE(NAME, LIVE, REPL, EXPECT)                                                         \
    do {                                                                                      \
        long differ = 0;                                                                      \
        if (!patch_on(&p, (void*)(LIVE), (void*)(REPL))) { printf("  FAIL: patch\n"); return 1; } \
        c_hit = 0;                                                                            \
        for (i = 0; i < NCASE; ++i) {                                                         \
            build_case(i);                                                                    \
            bm.SizeOfBitMap = cur_size;                                                       \
            if ((LIVE)(&bm, cur_n, cur_hint) != (EXPECT)[i]) ++differ;                        \
        }                                                                                     \
        printf("  [%-18s] %d cases, %ld differ;  our-code calls = %ld\n",                     \
               NAME, NCASE, differ, (long)c_hit);                                             \
        OK(differ == 0, NAME " answered differently under the patch");                        \
        OK(c_hit == (LONG)NCASE, NAME " counter did not move once per call");                 \
        OK(patch_off(&p), NAME " prologue was not restored byte-for-byte");                   \
    } while (0)

    ONE("RtlFindSetBits",   l_set, w_set, exp_s);
    ONE("RtlFindClearBits", l_clr, w_clr, exp_c);

    /* ---- and the whole corpus again, through the RESTORED exports ---- */
    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            bm.SizeOfBitMap = cur_size;
            if (l_set(&bm, cur_n, cur_hint) != exp_s[i]) ++post;
            if (l_clr(&bm, cur_n, cur_hint) != exp_c[i]) ++post;
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
