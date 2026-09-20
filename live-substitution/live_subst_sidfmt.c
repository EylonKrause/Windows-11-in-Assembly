// live-substitution/live_subst_sidfmt.c
// LIVE-RUN PROOF for change 067 (ntdll!RtlConvertSidToUnicodeString).
//
// What is compared is the whole destination, not the string. This export writes into the caller's
// buffer and its interesting behaviour is at the EDGES of that buffer:
//
//   * at MaximumLength == Length+1 it succeeds and writes NO TERMINATOR -- the two bytes past the
//     string keep the caller's fill (probes/oddroom.c). At Length+2 a full wide NUL appears.
//   * at anything below Length+1 it returns STATUS_BUFFER_OVERFLOW and leaves Out COMPLETELY
//     untouched -- not Length, not one byte of the buffer.
//
// Neither of those is visible to a check that compares the status, or the string, or the buffer up
// to Length. So every case here poison-fills the destination, records the NTSTATUS, Length,
// MaximumLength AND a hash of all 1024 bytes, and the MaximumLength for each case is drawn from the
// boundary rather than from "something ample" -- the gate this replaces used 600 for three million
// cases and stepped its one boundary sweep BY TWO, so it never once asked an odd value.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its three passes and reported 14285 differences with its patch counter at ZERO --
// the shipped export disagreeing with itself.
//
// The count is drawn over its whole byte range, 0..255, not 0..15. The old gate drew it as
// (seed>>8)%16 and therefore never expressed a count above 15 -- which the live export refuses and
// the old implementation did not, formatting all 200 sub-authorities of a SID whose count byte said
// 200 into a 400-byte stack temporary.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is used by neither the loader nor
//       the heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the whole corpus
//       is run again through the restored export.
//
// Build: build_sidfmt_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
typedef NTSTATUS (NTAPI *F_FMT)(U*, PSID, BOOLEAN);

extern NTSTATUS wia_sidfmt(U*, void*, BOOLEAN);

static volatile LONG c_hit;
static NTSTATUS NTAPI w_fmt(U* o, PSID s, BOOLEAN a)
{ _InterlockedIncrement(&c_hit); return wia_sidfmt(o, (void*)s, a); }

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
#define DBYTES 1024                       /* the destination, in bytes; the longest result is 366 */

static unsigned char sid[8 + 4 * 256];
static unsigned char dst[DBYTES];

typedef struct { NTSTATUS st; USHORT len, max; unsigned long long hash; } rec_t;
static rec_t expected[NCASE];

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

/* the digit-count boundaries, where a length-first converter is most likely to be off by one */
static const unsigned EDGE[] = {
    0u, 1u, 9u, 10u, 11u, 99u, 100u, 101u, 999u, 1000u, 9999u, 10000u, 99999u, 100000u,
    999999u, 1000000u, 9999999u, 10000000u, 99999999u, 100000000u, 999999999u, 1000000000u,
    2147483647u, 2147483648u, 4294967294u, 4294967295u
};
#define NEDGE (int)(sizeof EDGE / sizeof EDGE[0])

static int cur_cnt, cur_rev, cur_mlmode;
static USHORT cur_ml;

static void build_case(long i)
{
    int k, cls;
    unsigned long long auth;

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    cls = (int)(i % 5);
    /* the count over its whole byte range: mostly legal, but one case in nine is not */
    cur_cnt = ((i % 9) == 4) ? (int)(16 + rnd() % 240) : (int)(rnd() % 16);
    /* the revision is 1 except one case in eleven */
    cur_rev = ((i % 11) == 7) ? (int)(rnd() % 256) : 1;

    switch (cls) {
    case 0:  auth = rnd() % 32; break;                              /* the ordinary authorities */
    case 1:  auth = rnd();      break;                              /* decimal, many digits */
    case 2:  auth = 0xFFFFFFFFull - (rnd() % 4); break;             /* the decimal/hex boundary */
    case 3:  auth = 0x100000000ull + (rnd() % 4); break;
    default: auth = ((unsigned long long)rnd() << 16) & 0xFFFFFFFFFFFFull; break;
    }

    sid[0] = (unsigned char)cur_rev;
    sid[1] = (unsigned char)cur_cnt;
    for (k = 0; k < 6; ++k) sid[2 + k] = (unsigned char)(auth >> (8 * (5 - k)));
    for (k = 0; k < 16; ++k) {
        /* half the sub-authorities sit exactly on a digit-count boundary */
        unsigned v = (rnd() & 1) ? EDGE[rnd() % NEDGE] : rnd();
        sid[8 + 4 * k + 0] = (unsigned char)v;
        sid[8 + 4 * k + 1] = (unsigned char)(v >> 8);
        sid[8 + 4 * k + 2] = (unsigned char)(v >> 16);
        sid[8 + 4 * k + 3] = (unsigned char)(v >> 24);
    }

    /* MaximumLength: drawn AROUND the boundary, not from "something ample". The needed length is
       recomputed here rather than remembered, because the corpus must be a pure function of i. */
    {
        /* "S-1-" is four characters, which is EIGHT BYTES. The first version of this line said 4,
           so every "exactly Length+1" case landed four bytes short of the boundary and the harness
           reported ZERO of them -- and said so rather than passing with its most important class
           never once reached. */
        int need = 8;
        int d;
        unsigned long long a = auth;
        if (a < 0x100000000ull) { d = 1; { unsigned long long t = 10; while (a >= t) { t *= 10; ++d; } } }
        else { d = 2; { unsigned long long t = 1; int n = 0; while (a >= t) { t <<= 4; ++n; } d += n; } }
        need += 2 * d;
        if (cur_cnt <= 15) {
            for (k = 0; k < cur_cnt; ++k) {
                unsigned v = (unsigned)sid[8 + 4*k] | ((unsigned)sid[9 + 4*k] << 8) |
                             ((unsigned)sid[10 + 4*k] << 16) | ((unsigned)sid[11 + 4*k] << 24);
                int n = 1; unsigned long long t = 10;
                while (v >= t) { t *= 10; ++n; }
                need += 2 + 2 * n;
            }
        }
        cur_mlmode = (int)(rnd() % 6);
        switch (cur_mlmode) {
        case 0: cur_ml = (USHORT)(need - 1); break;     /* one short */
        case 1: cur_ml = (USHORT)need;       break;     /* exactly the string, still a refusal */
        case 2: cur_ml = (USHORT)(need + 1); break;     /* accepted, and NO terminator */
        case 3: cur_ml = (USHORT)(need + 2); break;     /* accepted, with one */
        case 4: cur_ml = (USHORT)(rnd() % 400); break;  /* anywhere at all */
        default: cur_ml = 800; break;                   /* ample */
        }
        if (cur_ml > DBYTES) cur_ml = DBYTES;
    }
}

static void run_case(F_FMT f, rec_t* out)
{
    U u;
    memset(dst, 0xA5, sizeof dst);
    u.Length = 0xBEEF; u.MaximumLength = cur_ml; u.Buffer = (wchar_t*)dst;
    out->st = f(&u, (PSID)sid, FALSE);
    out->len = u.Length;
    out->max = u.MaximumLength;
    out->hash = fnv(dst, sizeof dst);
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_FMT live = (F_FMT)GetProcAddress(h, "RtlConvertSidToUnicodeString");
    patch_t pt;
    long i;
    long n_ok = 0, n_ovf = 0, n_bad = 0, n_noterm = 0, n_big = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: ntdll!RtlConvertSidToUnicodeString (change 067) ==\n");

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live, &expected[i]);
        if (expected[i].st == 0) {
            ++n_ok;
            if (cur_mlmode == 2) ++n_noterm;
            if (expected[i].len > 200) ++n_big;
        }
        else if ((ULONG)expected[i].st == 0x80000005ul) ++n_ovf;
        else if ((ULONG)expected[i].st == 0xC0000078ul) ++n_bad;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  SUCCESS %ld (of which %ld at\n"
           "               exactly Length+1, where NO terminator is written, and %ld longer than 200\n"
           "               bytes, which is the 32-byte copy path), BUFFER_OVERFLOW %ld,\n"
           "               INVALID_SID %ld\n",
           NCASE, n_ok, n_noterm, n_big, n_ovf, n_bad);
    OK(n_ok     > 5000, "the corpus rarely succeeded");
    OK(n_ovf    > 3000, "the corpus rarely produced STATUS_BUFFER_OVERFLOW");
    OK(n_bad    > 2000, "the corpus rarely produced STATUS_INVALID_SID -- which is where the count\n"
                        "        above 15 and the revision other than 1 both land");
    OK(n_noterm > 1000, "the corpus rarely hit MaximumLength == Length+1, the one success that\n"
                        "        writes no terminator and the case the old gate could not express");
    OK(n_big    > 500,  "the corpus rarely produced a result long enough to take the 32-byte copy");

    {
        long differ = 0;
        rec_t got;
        if (!patch_on(&pt, (void*)live, (void*)w_fmt)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.st != expected[i].st || got.len != expected[i].len ||
                got.max != expected[i].max || got.hash != expected[i].hash) {
                if (++differ <= 8)
                    printf("  differ at %ld (rev %d, count %d, ml %u): live %08lX/%u  ours %08lX/%u%s\n",
                           i, cur_rev, cur_cnt, cur_ml,
                           (unsigned long)expected[i].st, expected[i].len,
                           (unsigned long)got.st, got.len,
                           got.hash != expected[i].hash ? "  (the destination differs)" : "");
            }
        }
        printf("  [patched]    %d cases, %ld differ (status, Length, MaximumLength and all %d bytes\n"
               "               of the destination);  our-code calls = %ld\n",
               NCASE, differ, DBYTES, (long)c_hit);
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

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (status, Length, MaximumLength and every one of the\n"
                      "%d destination bytes identical over 40000 cases, including the Length+1\n"
                      "success that writes no terminator; prologue restored byte-exact and the\n"
                      "corpus re-run through it)\n",
           failures ? failures : DBYTES);
    return failures ? 1 : 0;
}
