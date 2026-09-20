// live-substitution/live_subst_appasciiz.c
// LIVE-RUN PROOF for change 265 (ntdll!RtlAppendAsciizToString).
//
// The whole destination buffer is compared, not the status and not Length. This export writes into
// a caller's buffer and never writes a terminator -- unlike its wide sibling, which change 101
// landed and which does. The only way to catch an implementation that helpfully NUL-terminates is
// to fill the buffer with poison and compare every byte afterwards, so that is what this does, with
// a 64-bit FNV-1a fold of the whole buffer recorded for each case.
//
// Both outcomes are counted and the run fails if either is thin. a successful append and a refusal
// are different code paths, and only the refusal is allowed to leave the buffer untouched -- a
// corpus that never overflowed would be testing half the function.
//
// The corpus is regenerated from the case index on every pass, and it must be: this export mutates
// its destination, so a second pass over a destination the first pass filled would be asking a
// different question. Change 252's harness carried PRNG state across its passes and reported 14285
// differences with its counter at ZERO -- the shipped export disagreeing with itself.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is not used by the loader or heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_appasciiz_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } ASTR;
typedef LONG (NTAPI *F_App)(ASTR*, const char*);

extern LONG wia_appendasciiztostring(void*, const char*);

static volatile LONG c_hit;
static LONG NTAPI w_app(ASTR* d, const char* s)
{ _InterlockedIncrement(&c_hit); return wia_appendasciiztostring(d, s); }

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
#define CAP   9000
static char src[CAP + 16], dst[CAP + 16];
static USHORT cur_len0, cur_max;
static int cur_null;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int sl, k;
    rs = 0x452821E638D01377ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    /* every rung of the small-copy ladder gets its own share, not just the sizes a uniform
       distribution would land on */
    sl = ((i % 7) == 0) ? (int)(rnd() % 34) : (int)(rnd() % 3000);
    for (k = 0; k < sl; ++k) src[k] = (char)(0x21 + (rnd() % 94));
    src[sl] = 0;
    cur_len0 = (USHORT)(rnd() % 200);
    /* one case in three is made to NOT fit, so the refusal path is properly exercised */
    if ((i % 3) == 0) cur_max = (USHORT)(cur_len0 + (sl ? (rnd() % (unsigned)sl) : 0));
    else              cur_max = (USHORT)(cur_len0 + sl + (rnd() % 64));
    cur_null = ((i % 211) == 0);
    memset(dst, '#', sizeof dst);
    for (k = 0; k < cur_len0; ++k) dst[k] = (char)('A' + (k % 26));
}

static uint64_t hash_dst(void)
{
    uint64_t h = 1469598103934665603ull;
    int i;
    for (i = 0; i < CAP + 16; ++i) { h ^= (unsigned char)dst[i]; h *= 1099511628211ull; }
    return h;
}

static LONG     exp_st[NCASE];
static USHORT   exp_len[NCASE];
static uint64_t exp_h[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_App live = (F_App)GetProcAddress(h, "RtlAppendAsciizToString");
    patch_t p;
    long i, ok = 0, refused = 0, nulls = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlAppendAsciizToString (change 265) ==\n");

    for (i = 0; i < NCASE; ++i) {
        ASTR d;
        build_case(i);
        d.Length = cur_len0; d.MaximumLength = cur_max; d.Buffer = dst;
        exp_st[i] = live(&d, cur_null ? NULL : src);
        exp_len[i] = d.Length;
        exp_h[i] = hash_dst();
        if (exp_st[i] == 0) ++ok; else ++refused;
        if (cur_null) ++nulls;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED code;  appended %ld, REFUSED %ld, "
           "NULL source %ld\n", NCASE, ok, refused, nulls);
    OK(ok > 5000,      "the corpus rarely appended anything");
    OK(refused > 5000, "the corpus rarely overflowed -- the refusal path would be untested");
    OK(nulls > 50,     "the corpus never passed NULL");

    {
        long differ = 0;
        if (!patch_on(&p, (void*)live, (void*)w_app)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            ASTR d;
            build_case(i);
            d.Length = cur_len0; d.MaximumLength = cur_max; d.Buffer = dst;
            if (live(&d, cur_null ? NULL : src) != exp_st[i] ||
                d.Length != exp_len[i] || hash_dst() != exp_h[i]) ++differ;
        }
        printf("  [patched]    %d cases, %ld differ (status, Length AND the whole buffer);  "
               "our-code calls = %ld\n", NCASE, differ, (long)c_hit);
        OK(differ == 0, "RtlAppendAsciizToString answered or wrote differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");
        OK(patch_off(&p), "the prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            ASTR d;
            build_case(i);
            d.Length = cur_len0; d.MaximumLength = cur_max; d.Buffer = dst;
            if (live(&d, cur_null ? NULL : src) != exp_st[i] ||
                d.Length != exp_len[i] || hash_dst() != exp_h[i]) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (status, Length and the WHOLE buffer on every case,\n"
                      "both outcomes reached, proved by its own counter and restored byte-exact)\n",
           failures);
    return failures ? 1 : 0;
}
