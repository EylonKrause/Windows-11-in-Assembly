// live-substitution/live_subst_initu8.c
// LIVE-RUN PROOF for change 264 (ntdll!RtlInitUTF8String).
//
// The implementation is change 095's, reached through a linker ALIAS, probes/equiv.c proved
// RtlInitUTF8String identical to RtlInitString over 125883 cases, including every ordered byte
// pair. This run is what turns that into a claim about THIS export: the patch goes on
// RtlInitUTF8String at its own address, and every answer is compared against what the shipped code
// at that address produced before the patch existed.
//
// All three fields are compared against a struct poisoned with 0xCD before every call. Length alone
// would miss an implementation that forgot MaximumLength, and both would miss one that forgot the
// Buffer pointer, which the NULL case is specifically about, since it must write all three.
//
// The corpus reaches the clamp, and the run reports how many cases did. Above 65534 bytes the
// answer stops depending on the string, and an implementation that stopped SCANNING there would be
// faster and wrong; the corpus therefore contains strings on both sides of it.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its three passes and reported 14285 differences with its counter at ZERO, the
// shipped export disagreeing with itself, and that is the discipline this avoids.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll, never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is not used by the loader or heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_initu8_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } U8STR;
typedef void (NTAPI *F_Init)(U8STR*, const char*);

extern void wia_rtlinitutf8string(U8STR*, const char*);

static volatile LONG c_hit;
static void NTAPI w_init(U8STR* d, const char* s)
{ _InterlockedIncrement(&c_hit); wia_rtlinitutf8string(d, s); }

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
static char buf[70001];
static const char* cur;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int mode = (int)(i % 5), len, k;
    rs = 0x452821E638D01377ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    if ((i % 97) == 0) len = 65400 + (int)(rnd() % 300);      /* on and across the clamp */
    else               len = (int)(rnd() % 600);
    for (k = 0; k < len; ++k) {
        unsigned r = rnd();
        buf[k] = (mode == 0) ? (char)(0x20 + (r % 95))            /* ASCII */
               : (mode == 1) ? (char)(0x80 | (r & 0x7F))          /* high bit only */
               : (mode == 2) ? (char)(0x41 + (r % 26))            /* letters */
               : (mode == 3) ? (char)((r & 3) ? 0xC3 : 0xA9)      /* UTF-8 lead/continuation */
                             : (char)(r ? r : 1);                 /* anything but NUL */
    }
    buf[len] = 0;
    cur = ((i % 271) == 0) ? NULL : buf;                          /* and the NULL source */
}

static USHORT exp_len[NCASE], exp_max[NCASE];
static const char* exp_buf[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Init live = (F_Init)GetProcAddress(h, "RtlInitUTF8String");
    patch_t p;
    long i, nclamp = 0, nnull = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ntdll!RtlInitUTF8String (change 264) ==\n");

    for (i = 0; i < NCASE; ++i) {
        U8STR d;
        memset(&d, 0xCD, sizeof d);
        build_case(i);
        live(&d, cur);
        exp_len[i] = d.Length; exp_max[i] = d.MaximumLength; exp_buf[i] = d.Buffer;
        if (d.Length == 65534) ++nclamp;
        if (!cur) ++nnull;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED code;  %ld reached the CLAMP, "
           "%ld were NULL\n", NCASE, nclamp, nnull);
    OK(nclamp > 100, "the corpus never reached the clamp");
    OK(nnull > 50,   "the corpus never passed NULL, which must write all three fields");

    {
        long differ = 0;
        if (!patch_on(&p, (void*)live, (void*)w_init)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            U8STR d;
            memset(&d, 0xCD, sizeof d);
            build_case(i);
            live(&d, cur);
            if (d.Length != exp_len[i] || d.MaximumLength != exp_max[i] || d.Buffer != exp_buf[i])
                ++differ;
        }
        printf("  [patched]    %d cases, %ld differ (all three fields);  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "RtlInitUTF8String answered differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");
        OK(patch_off(&p), "the prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            U8STR d;
            memset(&d, 0xCD, sizeof d);
            build_case(i);
            live(&d, cur);
            if (d.Length != exp_len[i] || d.MaximumLength != exp_max[i] || d.Buffer != exp_buf[i])
                ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (all three fields on every case, the clamp and the\n"
                      "NULL source both reached, proved by its own counter and restored byte-exact)\n",
           failures);
    return failures ? 1 : 0;
}
