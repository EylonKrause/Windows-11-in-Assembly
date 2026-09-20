// live-substitution/live_subst_crc32.c
// LIVE-RUN PROOF for change 267 (ntdll!RtlCrc32).
//
// a checksum is the easiest thing in this project to get subtly wrong and the hardest to notice:
// one wrong constant gives a perfectly plausible 32-bit number at a perfectly plausible speed. So
// every case compares the exact value, and the corpus is built around the two block boundaries the
// implementation has, 192 bytes, where the short three-way split begins, and 3072, where the
// long one does, rather than around round numbers.
//
// The initial crc is non-zero in most cases, because it only enters the first of the three parallel
// chains. An implementation that seeded the wrong chain, or seeded all three, would pass every case
// that started from zero and fail here.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its three passes and reported 14285 differences with its counter at ZERO, the
// shipped export disagreeing with itself.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll, never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is not used by the loader or heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_crc32_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef ULONG (NTAPI *F_Crc32)(const void*, SIZE_T, ULONG);

extern ULONG wia_crc32(const void*, SIZE_T, ULONG);
extern int wia_crc32_tables_init(void);

static volatile LONG c_hit;
static ULONG NTAPI w_crc32(const void* p, SIZE_T n, ULONG init)
{ _InterlockedIncrement(&c_hit); return wia_crc32(p, n, init); }

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
#define CAP   20000
static unsigned char buf[CAP];
static SIZE_T cur_n;
static ULONG  cur_init;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    /* the lengths that matter are the ones either side of a block boundary, so they get their own
       share rather than being left to a uniform draw */
    static const SIZE_T EDGE[16] = { 0, 1, 7, 8, 63, 64, 65, 191, 192, 193, 1023, 1024,
                                     3071, 3072, 3073, 6144 };
    int k;
    rs = 0x452821E638D01377ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    cur_n = ((i % 4) == 0) ? EDGE[i % 16] : (SIZE_T)(rnd() % CAP);
    cur_init = ((i % 7) == 0) ? 0ul : rnd();
    for (k = 0; k < (int)cur_n; ++k) buf[k] = (unsigned char)rnd();
}

static ULONG exp_r[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Crc32 live = (F_Crc32)GetProcAddress(h, "RtlCrc32");
    patch_t p;
    long i, nonzero = 0, big = 0, smalln = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    if (wia_crc32_tables_init()) { printf("the shift tables failed their self-check\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlCrc32 (change 267) ==\n");

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        exp_r[i] = live(buf, cur_n, cur_init);
        if (cur_init) ++nonzero;
        if (cur_n >= 3072) ++big;
        if (cur_n < 192) ++smalln;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED code;  %ld with a NON-ZERO initial\n"
           "               CRC, %ld at or above the long block (3072), %ld below the short one (192)\n",
           NCASE, nonzero, big, smalln);
    OK(nonzero > 10000, "the corpus rarely used a non-zero initial CRC -- only the FIRST of the\n"
                        "        three parallel chains is seeded with it, so a zero-only corpus\n"
                        "        would not notice the wrong chain being seeded");
    OK(big > 2000,    "the corpus rarely reached the long-block path");
    OK(smalln > 1000, "the corpus rarely stayed below the short block, where the serial path runs");

    {
        long differ = 0;
        if (!patch_on(&p, (void*)live, (void*)w_crc32)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            if (live(buf, cur_n, cur_init) != exp_r[i]) ++differ;
        }
        printf("  [patched]    %d cases, %ld differ (the exact CRC);  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "RtlCrc32 answered differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");
        OK(patch_off(&p), "the prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            if (live(buf, cur_n, cur_init) != exp_r[i]) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (the exact CRC on every case, both block paths and\n"
                      "the serial one reached, non-zero initial CRCs throughout, restored byte-exact)\n",
           failures);
    return failures ? 1 : 0;
}
