// live-substitution/live_subst_lint2char.c
// LIVE-RUN PROOF for change 280 (ntdll!RtlLargeIntegerToChar).
//
// What is compared is the whole destination, not the status. probes/contract.c measured that a
// refusal leaves the caller's buffer COMPLETELY untouched (not one byte written) so an
// implementation that wrote a terminator before discovering it had no room would pass any check
// that only looked at the NTSTATUS. That is not hypothetical here: change 100, the landed
// implementation of this very export, differs from live on every negative length and six of change
// 279's eleven mutants had identical statuses on both sides.
//
// Five paths are under test:
//
//   * base 10 ABOVE 2^32, which peels eight digits at a time through the one 64-bit reciprocal
//     probes/div64.c proves over the whole domain;
//   * base 10 BELOW 2^32, which never enters that peel at all;
//   * the ONE-DIGIT decimal path, which writes its character and returns without touching a table;
//   * bases 2, 8 and 16, which emit several digits per store, base 2 running to SIXTY-FOUR
//     characters, twice the longest answer change 279 could produce;
//   * the zero-padded field width a negative length asks for, which is a fill loop no positive
//     length ever reaches and the only place this change touches an XMM register.
//
// A corpus of plausible positive lengths on plausible values would drive two of the five. So every
// case draws a base from the five legal ones AND the illegal ones, a value from a distribution that
// covers every one of the sixty-four bit lengths and the 2^32 and 10^8 boundaries specifically, and
// a length from around the room rule on both sides of zero. The harness fails if any path, either
// sign of length, or any of the three outcomes comes back thin.
//
// The negative lengths are bounded, and that bound is not timidity. a field width is honoured
// literally: probes/contract.c measured that -96 exactly fills a 96-byte buffer and -97 runs off
// the end of it, and change 279's first correctness corpus died of an access violation because it
// asked for INT_MIN+1; a field two billion characters wide. The destination here is 640 bytes and
// no case asks for more than 400.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded, patching only its own copy-on-write copy of
//       ntdll, never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is used by neither loader nor heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_lint2char_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_LI2C)(LARGE_INTEGER*, ULONG, LONG, char*);

extern NTSTATUS wia_lint2char(const LARGE_INTEGER*, ULONG, LONG, char*);

static volatile LONG c_hit;
static NTSTATUS NTAPI w_li2c(LARGE_INTEGER* v, ULONG b, LONG len, char* out)
{ _InterlockedIncrement(&c_hit); return wia_lint2char(v, b, len, out); }

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

#define NCASE  40000
#define DBUF   640
#define POISON 0x2A
#define MAXFLD 400              /* a field width is honoured literally; the buffer is 640 */

typedef struct { NTSTATUS st; unsigned long long hash; } rec_t;
static rec_t expected[NCASE];

static unsigned long long cur_v;
static ULONG cur_base;
static LONG  cur_len;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }
static unsigned long long rnd64(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

static unsigned long long fnv(const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    unsigned long long h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

static const ULONG LEGAL[] = { 0, 2, 8, 10, 16 };

/* Recompute how many characters this value needs in this base, so the length can be drawn from
   AROUND the boundary rather than from "something ample". */
static unsigned digits_of(unsigned long long v, ULONG b)
{
    unsigned n = 1;
    ULONG base = b ? b : 10;
    /* Base 1 Would loop forever: v /= 1 never decreases. One case in seven draws an illegal base
       from rnd() % 40, which includes 1, and change 278's harness hung in its pre-patch pass having
       printed only its header. The length of a refused call is never used, so any sane number will
       do -- but it has to terminate. */
    if (base < 2) return 1;
    while (v >= base) { v /= base; ++n; }
    return n;
}

static void build_case(long i)
{
    ULONG b;
    unsigned need;
    long len;

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    /* one case in seven uses an ILLEGAL base, co-prime with the class so every class gets some */
    if ((i % 7) == 3) cur_base = (ULONG)(rnd() % 40);
    else              cur_base = LEGAL[rnd() % 5];
    b = cur_base ? cur_base : 10;

    switch (i % 8) {
    case 0: cur_v = rnd64(); break;                                  /* anything, 64 bits wide */
    case 1: cur_v = rnd64() >> (rnd() % 64); break;                  /* EVERY bit length */
    case 2: cur_v = rnd() % 10; break;                               /* the one-digit path */
    case 3: cur_v = rnd();  break;                                   /* below 2^32: no peel */
    case 4: {                                                        /* a power of the base, +/- 1 */
        unsigned long long p = 1;
        unsigned k = rnd() % 64;
        while (k-- && p <= 0xFFFFFFFFFFFFFFFFull / b) p *= b;
        cur_v = p + (unsigned long long)(rnd() % 3) - 1;
        break;
    }
    case 5: cur_v = 0xFFFFFFFFFFFFFFFFull - (rnd() % 4); break;      /* the top of the range */
    case 6: {                                                        /* the 2^32 and 10^8 seams */
        static const unsigned long long SEAM[8] = {
            0xFFFFFFFFull, 0x100000000ull, 99999999ull, 100000000ull,
            10000000000000000ull, 9999999999999999ull,
            184467440737ull * 100000000ull, 0ull };
        cur_v = SEAM[rnd() % 8] + (unsigned long long)(rnd() % 3) - 1;
        break;
    }
    default: cur_v = 1ull << (rnd() % 64); break;                    /* a single bit */
    }

    /* the length, drawn from AROUND the room rule, which here is `digits`, with the terminator
       written only if one more byte is there -- and then given a sign. A NEGATIVE length is not an
       error: it is a zero-padded field of exactly that width. */
    need = digits_of(cur_v, cur_base);
    switch (rnd() % 8) {
    case 0: len = (long)need - 1; break;
    case 1: len = (long)need;     break;
    case 2: len = (long)need + 1; break;
    case 3: len = (long)need + 2; break;
    case 4: len = (long)(rnd() % 70); break;
    case 5: len = (long)(rnd() % MAXFLD); break;
    case 6: len = 0; break;                                          /* always a refusal */
    default: len = 200; break;
    }
    if (len < 0) len = 0;
    if (len > MAXFLD) len = MAXFLD;

    /* three cases in eight ask for the field-width form. INT_MIN is included deliberately: it is
       the ONE negative length that refuses, because it cannot be negated. */
    switch (rnd() % 8) {
    case 0: case 1: case 2: cur_len = -(LONG)len; break;
    case 3: cur_len = (i % 997) == 5 ? (LONG)0x80000000ul : (LONG)len; break;
    default: cur_len = (LONG)len; break;
    }
}

static void run_case(F_LI2C f, rec_t* out)
{
    static char buf[DBUF];
    LARGE_INTEGER q;
    q.QuadPart = (long long)cur_v;
    memset(buf, POISON, sizeof buf);
    out->st = f(&q, cur_base, cur_len, buf);
    out->hash = fnv(buf, sizeof buf);
}

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    F_LI2C live = (F_LI2C)GetProcAddress(hn, "RtlLargeIntegerToChar");
    patch_t pt;
    long i;
    long n_ok = 0, n_inval = 0, n_over = 0, n_peel = 0, n_nopeel = 0, n_one = 0;
    long n_p2 = 0, n_pad = 0, n_widepad = 0, n_b64 = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: ntdll!RtlLargeIntegerToChar (change 280) ==\n");

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live, &expected[i]);
        if (expected[i].st == 0) {
            ++n_ok;
            if (cur_base == 10 || cur_base == 0) {
                if (cur_v > 0xFFFFFFFFull) ++n_peel; else ++n_nopeel;
                if (cur_v < 10) ++n_one;
            } else {
                ++n_p2;
                if (cur_base == 2 && cur_v > 0xFFFFFFFFull) ++n_b64;
            }
            if (cur_len < 0) {
                ++n_pad;
                if (-(long)cur_len - (long)digits_of(cur_v, cur_base) >= 32) ++n_widepad;
            }
        }
        else if ((ULONG)expected[i].st == 0xC000000Dul) ++n_inval;
        else if ((ULONG)expected[i].st == 0x80000005ul) ++n_over;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  SUCCESS %ld:\n"
           "               base 10 above 2^32 (the eight-digit peel)      %ld\n"
           "               base 10 below 2^32 (no peel at all)            %ld\n"
           "               base 10 single digit (the short path)          %ld\n"
           "               a power-of-two base                            %ld  (of which %ld were\n"
           "               base 2 on a value above 2^32, up to 64 characters)\n"
           "               ZERO-PADDED field widths                       %ld  (of which %ld padded\n"
           "               32 bytes or more, which is the wide fill)\n"
           "               INVALID_PARAMETER %ld, BUFFER_OVERFLOW %ld\n",
           NCASE, n_ok, n_peel, n_nopeel, n_one, n_p2, n_b64, n_pad, n_widepad, n_inval, n_over);
    OK(n_ok      > 10000, "the corpus rarely succeeded");
    OK(n_peel    > 2000,  "the corpus barely used the eight-digit peel, which is the 64-bit\n"
                          "        reciprocal -- the whole reason this change is not change 279");
    OK(n_nopeel  > 1000,  "the corpus barely used a decimal value below 2^32, which skips the peel");
    OK(n_one     > 200,   "the corpus barely used the single-digit decimal path");
    OK(n_p2      > 3000,  "the corpus barely used a power-of-two base");
    OK(n_b64     > 200,   "the corpus barely produced a 64-character binary answer, which is the\n"
                          "        longest this export can write and twice change 279's");
    OK(n_pad     > 3000,  "the corpus barely used a NEGATIVE length, which is the zero-padded field\n"
                          "        width -- the write path no positive length ever reaches, and the\n"
                          "        one change 100 gets wrong on every single case");
    OK(n_widepad > 1000,  "the corpus barely padded 32 bytes or more, which is the wide fill loop");
    OK(n_inval   > 2000,  "the corpus rarely produced STATUS_INVALID_PARAMETER");
    OK(n_over    > 2000,  "the corpus rarely produced STATUS_BUFFER_OVERFLOW, which is the room rule");

    {
        long differ = 0;
        rec_t got;
        if (!patch_on(&pt, (void*)live, (void*)w_li2c)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.st != expected[i].st || got.hash != expected[i].hash) {
                if (++differ <= 8)
                    printf("  differ at %ld (v %llu, base %lu, len %ld): live %08lX  ours %08lX%s\n",
                           i, cur_v, (unsigned long)cur_base, (long)cur_len,
                           (unsigned long)expected[i].st, (unsigned long)got.st,
                           got.hash != expected[i].hash ? "  (the destination differs)" : "");
            }
        }
        printf("  [patched]    %d cases, %ld differ (the status AND every byte of the destination);"
               "\n               our-code calls = %ld\n", NCASE, differ, (long)c_hit);
        OK(differ == 0, "the export answered differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");
        OK(patch_off(&pt), "the prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG before = c_hit;
        rec_t got;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.st != expected[i].st || got.hash != expected[i].hash) ++post;
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
        printf("\nLIVE SUBSTITUTION: PASS (the status and every byte of a 640-byte destination\n"
               "identical over %d cases -- on refusing calls too, where the buffer must be\n"
               "untouched -- with the peel, the no-peel and single-digit decimal paths, all three\n"
               "power-of-two bases including 64-character binary, both signs of length, the wide\n"
               "and narrow fills and all three outcomes driven; prologue restored byte-exact and\n"
               "the corpus re-run through it)\n", NCASE);
    return failures ? 1 : 0;
}
