// live-substitution/live_subst_strchriw.c
// LIVE-RUN PROOF for change 281 (shlwapi!StrChrIW).
//
// What is compared is the returned offset, so "found it" and "found it in the right place" are the
// same question and a null can never accidentally agree with a pointer.
//
// All four dispatch paths are driven, because the needle decides which one runs and they share
// almost no code:
//
//   * 56825 needles match only themselves        -> one broadcast, one vpcmpeqw per 16 code units
//   * 5390 needles have 2..8 partners            -> four broadcasts when <= 4; an inline list at 5-8
//   * 3320 needles have more than eight, sharing
//     only eleven distinct sets between them     -> an 8 KB membership bitmap, one BT per code unit
//
// The relation is symmetric but not transitive -- U+D7B0 matches U+D7A2 and U+D7B1 matches U+D7A2,
// but U+D7B0 does not match U+D7B1, 168 intransitive triples in all. An earlier implementation
// grouped code units into classes and was wrong in 66 of 206096 gate cases, so this harness draws
// needles from every one of the four shapes explicitly rather than hoping a random draw reaches
// them: the bitmap needles are 3320 of 65535, which a uniform draw would hit about five per cent of
// the time and a short corpus might miss entirely.
//
// Every alignment is driven too. The implementation aligns its pointer down to 32 bytes and masks
// off everything before the true start; if that mask were off by one lane it would report a match
// in memory BEFORE the string. Each case picks its own start offset.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded, patching only its own copy-on-write copy of
//       shlwapi -- never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is used by neither loader nor heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_strchriw_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);

extern const wchar_t* wia_strchriw(const wchar_t*, wchar_t);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];

static volatile LONG c_hit;
static PCWSTR WINAPI w_chr(PCWSTR s, WCHAR c)
{ _InterlockedIncrement(&c_hit); return (PCWSTR)wia_strchriw(s, c); }

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
#define SBUF  600

static long expected[NCASE];
static wchar_t buf[SBUF + 64];

/* A GUARD PAGE, because two mutants survived this harness while gate 1 caught both.
 *
 * "the terminator is never looked for, so the scan runs past the end" passed here: every string
 * lived in the middle of a static buffer, so a scan that ignored the terminator simply walked into
 * zeroed memory and, often enough, still returned the same answer. A harness that cannot tell a
 * bounded scan from an unbounded one is not testing the thing that makes this implementation safe.
 *
 * One case in three is now placed so that its terminator is the LAST readable code unit before a
 * PAGE_NOACCESS page. An implementation that reads past the terminator dies here, which is exactly
 * what should happen.
 *
 * The other survivor, "the null-needle refusal is dropped", passed because the corpus never used
 * needle 0 -- the one needle the export refuses. build_case now forces it periodically. Both gaps
 * are the same gap: a corpus that could not express the case. */
static unsigned char* gbase;
static SIZE_T gpagesz;
static const wchar_t* cur_s;
static wchar_t cur_needle;
static int cur_shape;

/* one needle of each shape, chosen from the same table the implementation dispatches on */
static unsigned shape_needle[4];

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    unsigned n, k, start;
    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    n = 1 + rnd() % SBUF;
    start = rnd() % 32;                       /* every alignment of the aligned prologue */
    for (k = 0; k < n; ++k) {
        unsigned r = rnd();
        /* a mix of plain letters and arbitrary code units, so matches are common but not certain */
        buf[start + k] = (r & 3) ? (wchar_t)(L'a' + (r >> 8) % 26) : (wchar_t)(1 + (r >> 4) % 0xFFFF);
    }
    buf[start + n] = 0;
    cur_s = buf + start;

    /* one case in three ends exactly at a guard page, so a scan that ignores the terminator faults
       instead of quietly agreeing */
    if (gbase && (i % 3) == 1) {
        wchar_t* g = (wchar_t*)(gbase + gpagesz) - (n + 1);
        for (k = 0; k <= n; ++k) g[k] = buf[start + k];
        cur_s = g;
    }

    /* every fourth case forces one of the four dispatch shapes; the rest draw freely, so the
       common paths are exercised in bulk and the rare ones are guaranteed to appear at all */
    cur_shape = (int)(i & 3);
    if ((i % 37) == 5) cur_needle = 0;        /* the one needle the export refuses */
    else if ((i % 4) == 0) cur_needle = (wchar_t)shape_needle[(rnd() >> 3) & 3];
    else if (rnd() & 1) cur_needle = cur_s[rnd() % n];        /* a needle that is present */
    else cur_needle = (wchar_t)(1 + rnd() % 0xFFFF);
}

static long run_case(F_chr f)
{
    PCWSTR p = f(cur_s, cur_needle);
    return p ? (long)(p - cur_s) : -1;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    F_chr live = hs ? (F_chr)GetProcAddress(hs, "StrChrIW") : 0;
    patch_t pt;
    long i;
    long n_hit = 0, n_miss = 0, n_self = 0, n_small = 0, n_mid = 0, n_big = 0, n_zero = 0, n_guard = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("the tables disagree with the live export\n"); return 1; }
    printf("== LIVE SUBSTITUTION: shlwapi!StrChrIW (change 281) ==\n");
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        gpagesz = si.dwPageSize;
        gbase = (unsigned char*)VirtualAlloc(0, gpagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!gbase || !VirtualAlloc(gbase, gpagesz, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  FAIL: guard page setup\n"); return 1;
        }
        printf("  one case in three ends exactly at a PAGE_NOACCESS page, and one in 37 searches\n"
               "  for the terminator itself -- both gaps that let a mutant through this harness\n"
               "  while gate 1 caught it\n");
    }

    {
        unsigned k;
        for (k = 1; k < 0xFFFF; ++k) {
            unsigned n = wia_sci_n[k];
            if (!shape_needle[0] && n == 0 && k > 0x3000) shape_needle[0] = k;
            if (!shape_needle[1] && n >= 2 && n <= 4) shape_needle[1] = k;
            if (!shape_needle[2] && n >= 5 && n <= 8) shape_needle[2] = k;
            if (!shape_needle[3] && n == 255) shape_needle[3] = k;
        }
        printf("  the four dispatch shapes are driven by U+%04X (self only), U+%04X (2-4),\n"
               "  U+%04X (5-8) and U+%04X (bitmap)\n",
               shape_needle[0], shape_needle[1], shape_needle[2], shape_needle[3]);
    }

    for (i = 0; i < NCASE; ++i) {
        unsigned n;
        build_case(i);
        expected[i] = run_case(live);
        if (expected[i] >= 0) ++n_hit; else ++n_miss;
        if (!cur_needle) ++n_zero;
        if (gbase && cur_s >= (const wchar_t*)gbase) ++n_guard;
        n = wia_sci_n[(unsigned short)cur_needle];
        if (!cur_needle) continue;
        if (n == 0) ++n_self; else if (n == 255) ++n_big; else if (n <= 4) ++n_small; else ++n_mid;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  %ld hits, %ld misses\n"
           "               needle shapes: self-only %ld, 2-4 partners %ld, 5-8 partners %ld,\n"
           "               bitmap %ld\n", NCASE, n_hit, n_miss, n_self, n_small, n_mid, n_big);
    OK(n_hit   > 8000, "the corpus rarely found anything");
    OK(n_miss  > 4000, "the corpus rarely missed, which is the path that scans the whole string");
    OK(n_self  > 2000, "the corpus barely used a self-only needle");
    OK(n_small > 2000, "the corpus barely used a 2-4 partner needle, which is the AVX2 path");
    OK(n_mid   > 500,  "the corpus barely used a 5-8 partner needle, which is the inline list");
    OK(n_big   > 500,  "the corpus barely used a bitmap needle -- 3320 of 65535, which a uniform\n"
                       "        draw would reach only five per cent of the time");
    OK(n_zero  > 500,  "the corpus barely searched for the TERMINATOR, which the export refuses --\n"
                       "        a mutant that dropped that refusal survived this harness once");
    OK(n_guard > 8000, "the corpus barely used the guard page, which is the only thing here that\n"
                       "        can tell a bounded scan from an unbounded one");

    {
        long differ = 0;
        if (!patch_on(&pt, (void*)live, (void*)w_chr)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            long got;
            build_case(i);
            got = run_case(live);
            if (got != expected[i]) {
                if (++differ <= 8)
                    printf("  differ at %ld (needle U+%04X, len %d): live %ld  ours %ld\n",
                           i, (unsigned)cur_needle, (int)wcslen(cur_s), expected[i], got);
            }
        }
        printf("  [patched]    %d cases, %ld differ;  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "the export answered differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");
        OK(patch_off(&pt), "the prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            if (run_case(live) != expected[i]) ++post;
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
        printf("\nLIVE SUBSTITUTION: PASS (the returned offset identical over %d cases, with all\n"
               "four dispatch shapes, every 32-byte start alignment, the terminator as a needle\n"
               "and a guard page driven; prologue restored byte-exact and the corpus re-run\n"
               "through it)\n", NCASE);
    return failures ? 1 : 0;
}
