// live-substitution/live_subst_int2char.c
// LIVE-RUN PROOF for change 279 (ntdll!RtlIntegerToChar).
//
// What is compared is the whole destination, not the status. probes/contract.c measured that a
// refusal leaves the caller's buffer COMPLETELY untouched (not one byte written) so an
// implementation that helpfully wrote a terminator before discovering it had no room would pass any
// check that only looked at the NTSTATUS. That is change 268's rule, which found 154 mismatches in
// change 016 that were nothing but a single 00 past the end of a string.
//
// Three write paths are under test, not two:
//
//   * base 10 is length-first and two digits at a time;
//   * bases 2, 8 and 16 emit more than one digit per store from wide tables;
//   * a negative `length` is a zero-padded field width, probes/negative.c found it by sweeping
//     every negative length against a guard page, and it runs a fill loop that NO positive length
//     ever reaches. It is also the only part of this change that touches an XMM register.
//
// A corpus of plausible positive lengths would drive two of the three and report them as the
// function. So every case draws a base from the five legal ones AND the illegal ones, a value from
// the digit-count boundaries as often as from anywhere, and a length from AROUND the room rule on
// both sides of zero. The harness FAILS if any converter, either sign of length, or any of the
// three outcomes comes back thin.
//
// The negative lengths are bounded, and that bound is not timidity. a field width is honoured
// literally: probes/negative.c measured that length -100 writes a hundred characters and FAULTS if
// the buffer is shorter, and the first draft of change 279's correctness corpus died of an access
// violation because it asked for INT_MIN+1, a field two billion characters wide. The destination
// here is 512 bytes and no case asks for more than 300.
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
// Build: build_int2char_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_I2C)(ULONG, ULONG, LONG, char*);

extern NTSTATUS wia_int2char(ULONG, ULONG, LONG, char*);

static volatile LONG c_hit;
static NTSTATUS NTAPI w_i2c(ULONG v, ULONG b, LONG len, char* out)
{ _InterlockedIncrement(&c_hit); return wia_int2char(v, b, len, out); }

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
#define DBUF   512
#define POISON 0x2A
#define MAXFLD 300              /* a field width is honoured literally; the buffer is 512 */

typedef struct { NTSTATUS st; unsigned long long hash; } rec_t;
static rec_t expected[NCASE];

static ULONG cur_v, cur_base;
static LONG  cur_len;
static int   cur_cls;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

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
static unsigned digits_of(ULONG v, ULONG b)
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

    cur_cls = (int)(i % 6);

    /* one case in seven uses an ILLEGAL base, co-prime with the class so every class gets some */
    if ((i % 7) == 3) cur_base = (ULONG)(rnd() % 40);
    else              cur_base = LEGAL[rnd() % 5];
    b = cur_base ? cur_base : 10;

    switch (cur_cls) {
    case 0: cur_v = rnd(); break;                                  /* anything */
    case 1: cur_v = rnd() % 1000; break;                           /* short */
    case 2: {                                                      /* a power of the base, +/- 1 */
        unsigned long long p = 1;
        unsigned k = rnd() % 32;
        while (k-- && p <= 0xFFFFFFFFull / b) p *= b;
        cur_v = (ULONG)p + (ULONG)(rnd() % 3) - 1;
        break;
    }
    case 3: cur_v = 0xFFFFFFFFul - (rnd() % 4); break;             /* the top of the range */
    case 4: cur_v = 0; break;                                      /* zero, which is "0" in every base */
    default: cur_v = 1ul << (rnd() % 32); break;                   /* a single bit */
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
    case 4: len = (long)(rnd() % 45); break;
    case 5: len = (long)(rnd() % MAXFLD); break;
    case 6: len = 0; break;                                        /* which is always a refusal */
    default: len = 200; break;
    }
    if (len < 0) len = 0;
    if (len > MAXFLD) len = MAXFLD;

    /* three cases in eight ask for the field-width form. INT_MIN is included deliberately: it is the
       ONE negative length that refuses, because it cannot be negated. */
    switch (rnd() % 8) {
    case 0: case 1: case 2: cur_len = -(LONG)len; break;
    case 3: cur_len = (i % 997) == 5 ? (LONG)0x80000000ul : (LONG)len; break;
    default: cur_len = (LONG)len; break;
    }
}

static void run_case(F_I2C f, rec_t* out)
{
    static char buf[DBUF];
    memset(buf, POISON, sizeof buf);
    out->st = f(cur_v, cur_base, cur_len, buf);
    out->hash = fnv(buf, sizeof buf);
}

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    F_I2C live = (F_I2C)GetProcAddress(hn, "RtlIntegerToChar");
    patch_t pt;
    long i;
    long n_ok = 0, n_inval = 0, n_over = 0, n_b10 = 0, n_p2 = 0, n_pad = 0, n_widepad = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: ntdll!RtlIntegerToChar (change 279) ==\n");

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live, &expected[i]);
        if (expected[i].st == 0) {
            ++n_ok;
            if (cur_base == 10 || cur_base == 0) ++n_b10; else ++n_p2;
            if (cur_len < 0) {
                ++n_pad;
                if (-(long)cur_len - (long)digits_of(cur_v, cur_base) >= 32) ++n_widepad;
            }
        }
        else if ((ULONG)expected[i].st == 0xC000000Dul) ++n_inval;
        else if ((ULONG)expected[i].st == 0x80000005ul) ++n_over;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  SUCCESS %ld (of which %ld\n"
           "               took base 10, the length-first converter, and %ld a power-of-two base,\n"
           "               the wide multi-digit stores; %ld were ZERO-PADDED field widths, %ld of\n"
           "               those padded 32 bytes or more, which is the wide fill), \n"
           "               INVALID_PARAMETER %ld, BUFFER_OVERFLOW %ld\n",
           NCASE, n_ok, n_b10, n_p2, n_pad, n_widepad, n_inval, n_over);
    OK(n_ok      > 10000, "the corpus rarely succeeded");
    OK(n_b10     > 3000,  "the corpus barely used base 10, which is its own converter");
    OK(n_p2      > 3000,  "the corpus barely used a power-of-two base, which is the other converter");
    OK(n_pad     > 3000,  "the corpus barely used a NEGATIVE length, which is the zero-padded field\n"
                          "        width -- the write path no positive length ever reaches");
    OK(n_widepad > 1000,  "the corpus barely padded 32 bytes or more, which is the wide fill loop;\n"
                          "        the narrow overlapping-store path would be all that was tested");
    OK(n_inval   > 2000,  "the corpus rarely produced STATUS_INVALID_PARAMETER -- only five of the\n"
                          "        4294967296 possible bases are legal and the rest must be refused");
    OK(n_over    > 3000,  "the corpus rarely produced STATUS_BUFFER_OVERFLOW, which is the room rule");

    {
        long differ = 0;
        rec_t got;
        if (!patch_on(&pt, (void*)live, (void*)w_i2c)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.st != expected[i].st || got.hash != expected[i].hash) {
                if (++differ <= 8)
                    printf("  differ at %ld (v %lu, base %lu, len %ld): live %08lX  ours %08lX%s\n",
                           i, (unsigned long)cur_v, (unsigned long)cur_base, (long)cur_len,
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
        printf("\nLIVE SUBSTITUTION: PASS (the status and every byte of a 512-byte destination\n"
               "identical over %d cases -- on refusing calls too, where the buffer must be\n"
               "untouched -- with both converters, both signs of length, the wide and narrow fills\n"
               "and all three outcomes driven; prologue restored byte-exact and the corpus re-run\n"
               "through it)\n", NCASE);
    return failures ? 1 : 0;
}
