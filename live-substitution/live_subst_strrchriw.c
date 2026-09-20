// live-substitution/live_subst_strrchriw.c
// LIVE-RUN PROOF for change 282 (shlwapi!StrRChrIW).
//
// What is compared is the returned offset, so "found it" and "found it in the right place" are one
// question and a null can never accidentally agree with a pointer.
//
// This export has no terminator. probes/bounds.c measured that its end pointer is taken literally:
// "abcd\0fghijk" with end = start+11 finds 'J' at index 9, and an end pointer past a guard page
// FAULTS rather than stopping. So the corpus plants NULs inside ranges on purpose, and places
// ranges hard against a guard page at both ends, the scan may read neither at or after `end` nor
// before `start`, and those are two different mistakes.
//
// All four dispatch shapes from change 281's relation are driven explicitly rather than by a
// uniform draw: the bitmap needles are 3321 of 65536 and a short random corpus could miss them.
//
// And the range length is drawn small often, because a range that fits inside one 32-byte block is
// the case where both edge masks apply at once, the shape a mask written for two separate blocks
// gets wrong, and the one a corpus of long strings never reaches.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded, patching only its own copy-on-write copy of
//       shlwapi, never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is used by neither loader nor heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_strrchriw_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef PCWSTR (WINAPI *F_rchr)(PCWSTR, PCWSTR, WCHAR);

extern const wchar_t* wia_strrchriw(const wchar_t*, const wchar_t*, wchar_t);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];

static volatile LONG c_hit;
static PCWSTR WINAPI w_rchr(PCWSTR s, PCWSTR e, WCHAR c)
{ _InterlockedIncrement(&c_hit); return (PCWSTR)wia_strrchriw(s, e, c); }

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
static unsigned char *gend, *gstart;        /* ranges pinned to a guard page at each end */
static SIZE_T gpg;

static int cur_guard;
static const wchar_t* cur_s;
static const wchar_t* cur_e;
static wchar_t cur_needle;
static unsigned shape_needle[4];

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    unsigned n, k, start;
    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    /* half the ranges are SHORT, so the single-block case, where both edge masks apply at once --
       is reached in bulk rather than by luck */
    /* Empty ranges are in the corpus. a mutant that stopped rejecting them survived this harness
       because every range it built had at least one code unit in it. */
    n = ((i % 53) == 7) ? 0
      : ((rnd() & 1) ? (1 + rnd() % 24) : (1 + rnd() % SBUF));
    start = 32 + rnd() % 32;
    for (k = 0; k < n; ++k) {
        unsigned r = rnd();
        buf[start + k] = (r & 7) == 0 ? (wchar_t)0                 /* embedded NULs on purpose */
                       : ((r & 3) ? (wchar_t)(L'a' + (r >> 8) % 26)
                                  : (wchar_t)(1 + (r >> 4) % 0xFFFF));
    }
    /* The 32 Code units before the range are poisoned with a match. a mutant that dropped the
       bottom edge mask -- so the scan reported matches from BEFORE `start` -- survived this
       harness, because whatever happened to precede the range rarely matched. Now it always does. */
    for (k = 1; k <= 32 && start >= k; ++k) buf[start - k] = L'Z';
    cur_s = buf + start;
    cur_e = cur_s + n;
    cur_guard = 0;

    /* one case in three is pinned to a guard page, alternating which END of the range touches it */
    if (gend && n && (i % 3) == 1) {
        wchar_t* g = (wchar_t*)(gend + gpg) - n;
        for (k = 0; k < n; ++k) g[k] = buf[start + k];
        cur_s = g; cur_e = g + n; cur_guard = 1;
    } else if (gstart && n && (i % 3) == 2) {
        wchar_t* g = (wchar_t*)(gstart + gpg);
        for (k = 0; k < n; ++k) g[k] = buf[start + k];
        cur_s = g; cur_e = g + n; cur_guard = 1;
    }

    if ((i % 4) == 0) cur_needle = (wchar_t)shape_needle[(rnd() >> 3) & 3];
    else if (n && (rnd() & 1)) cur_needle = cur_s[rnd() % n];
    else if ((i % 11) == 3) cur_needle = L'z';   /* matches the poison before the range */
    else cur_needle = (wchar_t)(rnd() % 0x10000);
}

/* BYTE offsets, not code-unit offsets: a mutant that returned a pointer one BYTE off, into the
   middle of a wchar_t, because BSR reports the high byte of a matching word -- survived this
   harness AND gate 1, because `p - cur_s` on a wchar_t* divides the odd byte away. */
static long run_case(F_rchr f)
{
    PCWSTR p = f(cur_s, cur_e, cur_needle);
    return p ? (long)((const char*)p - (const char*)cur_s) : -1;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    F_rchr live = hs ? (F_rchr)GetProcAddress(hs, "StrRChrIW") : 0;
    patch_t pt;
    SYSTEM_INFO si;
    long i;
    long n_hit = 0, n_miss = 0, n_self = 0, n_small = 0, n_mid = 0, n_big = 0;
    long n_short = 0, n_guard = 0, n_nul = 0, n_empty = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }
    printf("== LIVE SUBSTITUTION: shlwapi!StrRChrIW (change 282) ==\n");

    GetSystemInfo(&si);
    gpg = si.dwPageSize;
    gend = (unsigned char*)VirtualAlloc(0, gpg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!gend || !VirtualAlloc(gend, gpg, MEM_COMMIT, PAGE_READWRITE)) { printf("  guard A\n"); return 1; }
    gstart = (unsigned char*)VirtualAlloc(0, gpg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!gstart || !VirtualAlloc(gstart + gpg, gpg, MEM_COMMIT, PAGE_READWRITE)) { printf("  guard B\n"); return 1; }
    printf("  one case in three is pinned to a PAGE_NOACCESS page, alternating which END of the\n"
           "  range touches it -- reading past `end` and reading before `start` are two different\n"
           "  mistakes and this export faults on both\n");

    {
        unsigned k;
        for (k = 1; k < 0xFFFF; ++k) {
            unsigned n = wia_sci_n[k];
            if (!shape_needle[0] && n == 0 && k > 0x3000) shape_needle[0] = k;
            if (!shape_needle[1] && n >= 2 && n <= 4) shape_needle[1] = k;
            if (!shape_needle[2] && n >= 5 && n <= 8) shape_needle[2] = k;
            if (!shape_needle[3] && n == 255) shape_needle[3] = k;
        }
    }

    for (i = 0; i < NCASE; ++i) {
        unsigned n;
        build_case(i);
        expected[i] = run_case(live);
        if (expected[i] >= 0) ++n_hit; else ++n_miss;
        if (cur_e - cur_s <= 16) ++n_short;
        if (cur_e == cur_s) ++n_empty;
        if (cur_guard) ++n_guard;   /* a FLAG, not a pointer comparison: the first version
                                       compared cur_s against both arenas and counted every case */
        {
            const wchar_t* q;
            for (q = cur_s; q < cur_e; ++q) if (!*q) { ++n_nul; break; }
        }
        n = wia_sci_n[(unsigned short)cur_needle];
        if (n == 0) ++n_self; else if (n == 255) ++n_big; else if (n <= 4) ++n_small; else ++n_mid;
    }
    printf("  [pre-patch]  %d cases;  %ld hits, %ld misses\n"
           "               needle shapes: self-only %ld, 2-4 %ld, 5-8 %ld, bitmap %ld\n"
           "               ranges of 16 code units or fewer (both masks at once): %ld\n"
           "               ranges holding an embedded NUL: %ld,  pinned to a guard page: %ld\n",
           NCASE, n_hit, n_miss, n_self, n_small, n_mid, n_big, n_short, n_nul, n_guard);
    OK(n_hit   > 8000, "the corpus rarely found anything");
    OK(n_miss  > 4000, "the corpus rarely missed, which is the path that scans the whole range");
    OK(n_self  > 2000, "the corpus barely used a self-only needle");
    OK(n_small > 2000, "the corpus barely used a 2-4 partner needle, which is the AVX2 path");
    OK(n_mid   > 300,  "the corpus barely used a 5-8 partner needle");
    OK(n_big   > 300,  "the corpus barely used a bitmap needle");
    OK(n_short > 8000, "the corpus barely used a range inside ONE 32-byte block, which is where\n"
                       "        both edge masks apply at once");
    OK(n_nul   > 5000, "the corpus barely planted an embedded NUL, and this export has no terminator");
    OK(n_guard > 20000, "the corpus barely used the guard pages");
    OK(n_empty > 400,   "the corpus contained almost no EMPTY ranges -- a mutant that stopped\n"
                        "        rejecting them survived this harness once");

    {
        long differ = 0;
        if (!patch_on(&pt, (void*)live, (void*)w_rchr)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            long got;
            build_case(i);
            got = run_case(live);
            if (got != expected[i]) {
                if (++differ <= 8)
                    printf("  differ at %ld (needle U+%04X, len %d): live %ld  ours %ld\n",
                           i, (unsigned)cur_needle, (int)(cur_e - cur_s), expected[i], got);
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
               "four dispatch shapes, both edge masks, embedded NULs and a guard page at each end\n"
               "of the range driven; prologue restored byte-exact and the corpus re-run)\n", NCASE);
    return failures ? 1 : 0;
}
