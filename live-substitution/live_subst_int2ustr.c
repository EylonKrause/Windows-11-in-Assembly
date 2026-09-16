// live-substitution/live_subst_int2ustr.c
// LIVE-RUN PROOF for change 278 (ntdll!RtlIntegerToUnicodeString).
//
// WHAT IS COMPARED IS THE WHOLE DESTINATION, not the status. probes/contract.c measured that a
// refusal leaves the UNICODE_STRING COMPLETELY untouched -- Length keeps whatever the caller had in
// it, and not one character is written -- so an implementation that helpfully zeroed Length on the
// way out would pass any check that only looked at the NTSTATUS. That is change 268's rule, which
// found 154 mismatches in change 016 that were nothing but a single 00 past the end of a string.
//
// THE CORPUS IS THE BASES AND THE BOUNDARIES. Two converters are under test: base 10 is
// length-first and two digits at a time, and bases 2, 8 and 16 share a shift-and-mask loop. A corpus
// of plausible decimal numbers would exercise one of the two. So every case draws a base from the
// five legal ones AND the illegal ones, a value from the digit-count boundaries as often as from
// anywhere, and a MaximumLength from AROUND the room rule -- which is Length+2 here and Length+1 in
// the routine change 067 owns, one byte apart in the same DLL.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG
// state across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded, patching only its own copy-on-write copy of
//       ntdll -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and this export is used by neither loader nor heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_int2ustr_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef NTSTATUS (NTAPI *F_I2US)(ULONG, ULONG, USTR*);

extern NTSTATUS wia_int2ustr(ULONG, ULONG, USTR*);

static volatile LONG c_hit;
static NTSTATUS NTAPI w_i2us(ULONG v, ULONG b, USTR* u)
{ _InterlockedIncrement(&c_hit); return wia_int2ustr(v, b, u); }

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
#define DBUF   64
#define POISON 0x2A2A

typedef struct { NTSTATUS st; USHORT len, max; unsigned long long hash; } rec_t;
static rec_t expected[NCASE];

static ULONG  cur_v, cur_base;
static USHORT cur_max;
static int    cur_cls;

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

/* Recompute how many characters this value needs in this base, so MaximumLength can be drawn from
   AROUND the boundary rather than from "something ample". */
static unsigned digits_of(ULONG v, ULONG b)
{
    unsigned n = 1;
    ULONG base = b ? b : 10;
    /* BASE 1 WOULD LOOP FOREVER: v /= 1 never decreases. One case in seven draws an ILLEGAL base
       from rnd() % 40, which includes 1, and the first version of this harness hung in the
       pre-patch pass having printed only its header. The length of a refused call is never used, so
       any sane number will do -- but it has to terminate. */
    if (base < 2) return 1;
    while (v >= base) { v /= base; ++n; }
    return n;
}

static void build_case(long i)
{
    ULONG b;
    unsigned need;

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    cur_cls = (int)(i % 6);

    /* one case in seven uses an ILLEGAL base -- co-prime with the class so every class gets some */
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

    /* MaximumLength drawn from AROUND the room rule, which is Length+2 */
    need = digits_of(cur_v, cur_base) * 2 + 2;
    switch (rnd() % 6) {
    case 0: cur_max = (USHORT)(need - 2); break;
    case 1: cur_max = (USHORT)(need - 1); break;
    case 2: cur_max = (USHORT)need;       break;
    case 3: cur_max = (USHORT)(need + 1); break;
    case 4: cur_max = (USHORT)(rnd() % 90); break;
    default: cur_max = 120; break;
    }
    if (cur_max > DBUF * 2) cur_max = DBUF * 2;
}

static void run_case(F_I2US f, rec_t* out)
{
    static wchar_t buf[DBUF];
    USTR u;
    int i;
    for (i = 0; i < DBUF; ++i) buf[i] = POISON;
    u.Length = 0xBEEF;
    u.MaximumLength = cur_max;
    u.Buffer = buf;
    out->st = f(cur_v, cur_base, &u);
    out->len = u.Length;
    out->max = u.MaximumLength;
    out->hash = fnv(buf, sizeof buf);
}

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    F_I2US live = (F_I2US)GetProcAddress(hn, "RtlIntegerToUnicodeString");
    patch_t pt;
    long i;
    long n_ok = 0, n_inval = 0, n_over = 0, n_b10 = 0, n_p2 = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: ntdll!RtlIntegerToUnicodeString (change 278) ==\n");

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live, &expected[i]);
        if (expected[i].st == 0) {
            ++n_ok;
            if (cur_base == 10 || cur_base == 0) ++n_b10; else ++n_p2;
        }
        else if ((ULONG)expected[i].st == 0xC000000Dul) ++n_inval;
        else if ((ULONG)expected[i].st == 0x80000005ul) ++n_over;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  SUCCESS %ld (of which %ld\n"
           "               took base 10, the length-first converter, and %ld a power-of-two base,\n"
           "               the shift-and-mask loop), INVALID_PARAMETER %ld, BUFFER_OVERFLOW %ld\n",
           NCASE, n_ok, n_b10, n_p2, n_inval, n_over);
    OK(n_ok    > 10000, "the corpus rarely succeeded");
    OK(n_b10   > 3000,  "the corpus barely used base 10, which is its own converter");
    OK(n_p2    > 3000,  "the corpus barely used a power-of-two base, which is the other converter");
    OK(n_inval > 2000,  "the corpus rarely produced STATUS_INVALID_PARAMETER -- only five of the\n"
                        "        4294967296 possible bases are legal and the rest must be refused");
    OK(n_over  > 3000,  "the corpus rarely produced STATUS_BUFFER_OVERFLOW, which is the room rule");

    {
        long differ = 0;
        rec_t got;
        if (!patch_on(&pt, (void*)live, (void*)w_i2us)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.st != expected[i].st || got.len != expected[i].len ||
                got.max != expected[i].max || got.hash != expected[i].hash) {
                if (++differ <= 8)
                    printf("  differ at %ld (v %lu, base %lu, max %u): live %08lX/%u  ours %08lX/%u%s\n",
                           i, (unsigned long)cur_v, (unsigned long)cur_base, cur_max,
                           (unsigned long)expected[i].st, expected[i].len,
                           (unsigned long)got.st, got.len,
                           got.hash != expected[i].hash ? "  (the destination differs)" : "");
            }
        }
        printf("  [patched]    %d cases, %ld differ (status, Length, MaximumLength and the WHOLE\n"
               "               destination);  our-code calls = %ld\n", NCASE, differ, (long)c_hit);
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
            if (got.st != expected[i].st || got.len != expected[i].len ||
                got.max != expected[i].max || got.hash != expected[i].hash) ++post;
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
        printf("\nLIVE SUBSTITUTION: PASS (the status, Length, MaximumLength and every byte of the\n"
               "destination identical over %d cases -- on refusing calls too, where the whole\n"
               "structure must be untouched -- with both converters and all three outcomes driven;\n"
               "prologue restored byte-exact and the corpus re-run through it)\n", NCASE);
    return failures ? 1 : 0;
}
