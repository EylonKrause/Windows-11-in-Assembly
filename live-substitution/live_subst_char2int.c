// live-substitution/live_subst_char2int.c
// LIVE-RUN PROOF for change 129 (ntdll!RtlCharToInteger).
//
// This change was PARKED before gate 4 existed and is being landed now, so this is the first time the
// shipped export is actually replaced by it. The corpus is built around the things that made the change
// hard, not around "some strings":
//
//   * The refusal must not write the caller's ulong. An invalid base returns STATUS_INVALID_PARAMETER
//     and leaves *Value exactly as it was -- measured, and the single most likely thing for a
//     replacement to get wrong, because zeroing the output first is the natural way to write the code.
//     Every case therefore pre-poisons *Value with a sentinel and compares the WORD as well as the
//     status. A harness that only read the NTSTATUS would pass an implementation that helpfully zeroed
//     the output on every refusal.
//
//   * Bases on both sides of 16. The landing edit validates a caller-supplied base with a range test
//     (`cmp edx,16 / ja`) plus a bitmask over bits 2, 8 and 16, replacing a four-compare ladder. That
//     splits the invalid bases into two populations -- above 16, refused by the range test, and below 16
//     but not in the set, refused by the mask -- and a corpus drawing only base 36 would exercise one of
//     them. Both are drawn deliberately, including 0xFFFFFFFF.
//
//   * The three prefixes are lowercase only, and the landing edit replaced three compares with one
//     windowed range test biased by 'b'. So the corpus needs "0x"/"0b"/"0o" AND "0X"/"0B"/"0O" (which
//     are NOT prefixes and parse as a bare 0), AND a leading '0' followed by something outside the
//     b..x window entirely ("0777", which means DECIMAL 777, not octal). Those last two are exactly the
//     rows that were below parity and are now above it.
//
//   * The leading skip uses a signed char compare, so it skips 0x80-0xFF as well as 0x01-0x20. a corpus
//     of spaces and tabs would never touch that, and it is the kind of rule that only shows up when a
//     high byte is planted.
//
//   * Bases <= 10 Now stop at the first non-digit without decoding a letter. Strings that end in a
//     letter, under a base that cannot accept letters, are the cases that distinguishes that shortcut
//     from the old path.
//
// The corpus is regenerated from the case index on every pass and carries no prng state across passes.
// Change 252's harness carried state and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded, patching only its own copy-on-write copy of
//       ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this export is used by neither loader nor heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_char2int_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_C2I)(const char*, ULONG, ULONG*);

extern long wia_char2int(const char*, unsigned long, unsigned long*);

static volatile LONG c_hit;
static NTSTATUS NTAPI w_c2i(const char* s, ULONG b, ULONG* v)
{ _InterlockedIncrement(&c_hit); return (NTSTATUS)wia_char2int(s, b, (unsigned long*)v); }

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
#define SENT   0xA5A5A5A5ul          /* the sentinel a refusal must leave in place */

typedef struct { NTSTATUS st; ULONG val; } rec_t;
static rec_t expected[NCASE];

static char  cur_s[40];
static ULONG cur_base;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

/* the legal bases, and the two populations of illegal one that the range test and the mask separate */
static const ULONG LEGAL[]    = { 0, 2, 8, 10, 16 };
static const ULONG ILL_LOW[]  = { 1, 3, 4, 5, 6, 7, 9, 11, 12, 13, 14, 15 };   /* refused by the mask */
/* Refused by the range test -- and the last four matter more than they look. impl.asm validates with
   a range test plus `bt r10d, edx`, and BT takes its bit index MODULO 32, so 0x80000002 indexes bit 2,
   a set bit in the mask. Only the unsigned range test keeps it out. Mutation mutant #22 made that test
   signed and survived both gates, because the largest base here was 0xFFFFFFFF, whose low five bits are
   31 and clear. */
static const ULONG ILL_HIGH[] = { 17, 20, 32, 36, 64, 255, 1000, 0x10000, 0xFFFFFFFFul,
                                  0x80000002ul, 0x80000008ul, 0x80000010ul, 0xFFFFFFE2ul };

static void build_case(long i)
{
    int j = 0, n, k;

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    /* the base: two cases in nine illegal, split between the two refusal mechanisms */
    switch (i % 9) {
    case 4:  cur_base = ILL_LOW[rnd() % (sizeof(ILL_LOW) / sizeof(ILL_LOW[0]))];   break;
    case 7:  cur_base = ILL_HIGH[rnd() % (sizeof(ILL_HIGH) / sizeof(ILL_HIGH[0]))]; break;
    default: cur_base = LEGAL[rnd() % 5];                                          break;
    }

    /* an optional leading run that the SIGNED-char skip must eat: spaces, controls, and HIGH BYTES */
    if ((i % 5) == 1) {
        n = 1 + (int)(rnd() % 3);
        for (k = 0; k < n; ++k) {
            switch (rnd() % 6) {
            case 0: cur_s[j++] = ' ';  break;
            case 1: cur_s[j++] = '\t'; break;
            case 2: cur_s[j++] = '\n'; break;
            case 3: cur_s[j++] = (char)0x01; break;
            case 4: cur_s[j++] = (char)0x80; break;      /* negative as a signed char: skipped */
            default: cur_s[j++] = (char)0xFF; break;     /* likewise */
            }
        }
    }

    /* an optional sign */
    switch (i % 11) {
    case 2: cur_s[j++] = '-'; break;
    case 5: cur_s[j++] = '+'; break;
    case 8: cur_s[j++] = '-'; cur_s[j++] = ' '; break;   /* "- 42": whitespace AFTER the sign stops it */
    default: break;
    }

    /* the body. The prefix forms matter most, and their uppercase twins matter just as much because
       they are NOT prefixes. */
    switch (i % 13) {
    case 0:                                         /* a lowercase prefix: a real base override */
        cur_s[j++] = '0';
        cur_s[j++] = (char)"xbo"[rnd() % 3];
        break;
    case 1:                                         /* the UPPERCASE twin: not a prefix at all */
        cur_s[j++] = '0';
        cur_s[j++] = (char)"XBO"[rnd() % 3];
        break;
    case 2:                                         /* a leading 0 then a digit: DECIMAL, not octal */
        cur_s[j++] = '0';
        break;
    case 3:                                         /* a leading 0 then a letter outside b..x */
        cur_s[j++] = '0';
        cur_s[j++] = (char)('A' + (rnd() % 26));
        break;
    case 4:                                         /* a bare 0, or "00" */
        cur_s[j++] = '0';
        if (rnd() & 1) cur_s[j++] = '0';
        break;
    default: break;
    }

    n = (int)(rnd() % 12);                          /* 0 digits is a real case: still SUCCESS, value 0 */
    for (k = 0; k < n; ++k) {
        unsigned r = rnd() % 26;
        if (r < 10)      cur_s[j++] = (char)('0' + r);
        else if (r < 16) cur_s[j++] = (char)('a' + r - 10);
        else if (r < 22) cur_s[j++] = (char)('A' + r - 16);   /* uppercase hex digits are accepted */
        else             cur_s[j++] = (char)"zg_."[r - 22];   /* stops the parse; base<=10 shortcut */
    }

    /* every eleventh case is a big decimal, so the silent mod-2^32 wrap is exercised */
    if ((i % 11) == 7) {
        j = 0;
        for (k = 0; k < 15 + (int)(rnd() % 6); ++k) cur_s[j++] = (char)('0' + (rnd() % 10));
        cur_base = 10;
    }

    if (j > 38) j = 38;
    cur_s[j] = 0;
}

static void run_case(F_C2I f, rec_t* out)
{
    ULONG v = SENT;
    out->st = f(cur_s, cur_base, &v);
    out->val = v;
}

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    F_C2I live = (F_C2I)GetProcAddress(hn, "RtlCharToInteger");
    patch_t pt;
    long i;
    long n_ok = 0, n_inval = 0, n_untouched = 0, n_pfx = 0, n_upfx = 0, n_zerolead = 0;
    long n_high = 0, n_b16 = 0, n_ble10 = 0, n_illlow = 0, n_illhigh = 0, n_wrap = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: ntdll!RtlCharToInteger (change 129) ==\n");

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live, &expected[i]);
        if (expected[i].st == 0) ++n_ok;
        else if ((ULONG)expected[i].st == 0xC000000Dul) {
            ++n_inval;
            if (expected[i].val == SENT) ++n_untouched;
        }
        if (cur_base == 16) ++n_b16;
        if (cur_base <= 10 && cur_base != 0) ++n_ble10;
        {
            int k, lo = 0, hi = 0;
            for (k = 0; k < (int)(sizeof(ILL_LOW) / sizeof(ILL_LOW[0])); ++k)
                if (cur_base == ILL_LOW[k]) lo = 1;
            for (k = 0; k < (int)(sizeof(ILL_HIGH) / sizeof(ILL_HIGH[0])); ++k)
                if (cur_base == ILL_HIGH[k]) hi = 1;
            n_illlow  += lo;
            n_illhigh += hi;
        }
        {
            const char* p = cur_s;
            while (*p && ((signed char)*p <= ' ')) ++p;
            if (p != cur_s && (unsigned char)cur_s[0] >= 0x80) ++n_high;
            if (*p == '+' || *p == '-') ++p;
            if (p[0] == '0') {
                ++n_zerolead;
                if (p[1] == 'x' || p[1] == 'b' || p[1] == 'o') ++n_pfx;
                if (p[1] == 'X' || p[1] == 'B' || p[1] == 'O') ++n_upfx;
            }
        }
        if (expected[i].st == 0 && strlen(cur_s) >= 15) ++n_wrap;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export\n", NCASE);
    printf("               SUCCESS %ld,  INVALID_PARAMETER %ld (of which %ld left *Value at its\n"
           "               sentinel, which is the rule a replacement is most likely to break)\n",
           n_ok, n_inval, n_untouched);
    printf("               bases: 16 -> %ld,  2/8/10 -> %ld,  illegal below 16 -> %ld,  above 16 -> %ld\n",
           n_b16, n_ble10, n_illlow, n_illhigh);
    printf("               leading '0' -> %ld,  of those a LOWERCASE prefix %ld and an UPPERCASE\n"
           "               non-prefix %ld;  high-byte skips %ld;  15+ digit wraps %ld\n",
           n_zerolead, n_pfx, n_upfx, n_high, n_wrap);

    OK(n_ok        > 20000, "the corpus rarely succeeded");
    OK(n_inval     > 3000,  "the corpus rarely produced STATUS_INVALID_PARAMETER");
    OK(n_untouched == n_inval,
       "a refusal wrote the caller's ULONG -- the SHIPPED export leaves it alone, so either the\n"
       "        contract changed or this harness is measuring the wrong thing");
    OK(n_illlow    > 1000,  "the corpus barely used an illegal base BELOW 16, which the bitmask refuses");
    OK(n_illhigh   > 1000,  "the corpus barely used an illegal base ABOVE 16, which the range test\n"
                            "        refuses -- a different mechanism, and both must be driven");
    OK(n_b16       > 3000,  "the corpus barely used base 16, the only base that decodes letters");
    OK(n_ble10     > 5000,  "the corpus barely used a base <= 10, where a letter can never be a digit\n"
                            "        and the loop takes its early-stop shortcut");
    OK(n_pfx       > 1000,  "the corpus barely used a LOWERCASE 0x/0b/0o prefix");
    OK(n_upfx      > 1000,  "the corpus barely used an UPPERCASE 0X/0B/0O, which is NOT a prefix and\n"
                            "        parses as a bare 0 -- the case the windowed range test must reject");
    OK(n_zerolead  > 3000,  "the corpus barely used a leading '0'");
    OK(n_high      > 500,   "the corpus barely planted a HIGH BYTE for the signed-char skip to eat");
    OK(n_wrap      > 500,   "the corpus barely used a 15+ digit decimal, which is the silent mod-2^32 wrap");

    {
        long differ = 0, differ_val = 0;
        rec_t got;
        if (!patch_on(&pt, (void*)live, (void*)w_c2i)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.st != expected[i].st || got.val != expected[i].val) {
                if (got.val != expected[i].val) ++differ_val;
                if (++differ <= 8) {
                    /* Printed as hex bytes, not with %s. The corpus deliberately plants 0x80-0xFF for
                       the signed-char skip to eat, and the console renders none of them -- the first
                       run of this harness reported eight failures whose strings all looked EMPTY, which
                       made them undiagnosable and very nearly sent me looking at the wrong case. A
                       diagnostic that cannot show the input it is complaining about is not a
                       diagnostic. */
                    int q;
                    printf("  differ at %ld (base %lu) bytes:", i, (unsigned long)cur_base);
                    for (q = 0; cur_s[q] && q < 24; ++q) printf(" %02X", (unsigned char)cur_s[q]);
                    printf("  | as text: \"");
                    for (q = 0; cur_s[q] && q < 24; ++q)
                        putchar((unsigned char)cur_s[q] >= 0x20 && (unsigned char)cur_s[q] < 0x7F
                                ? cur_s[q] : '.');
                    printf("\"\n                 live %08lX/%08lX  ours %08lX/%08lX\n",
                           (unsigned long)expected[i].st, (unsigned long)expected[i].val,
                           (unsigned long)got.st, (unsigned long)got.val);
                }
            }
        }
        printf("  [patched]    %d cases, %ld differ (the status AND the ULONG; %ld of those in the\n"
               "               value);  our-code calls = %ld\n", NCASE, differ, differ_val, (long)c_hit);
        OK(differ == 0, "the export answered differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");
        OK(patch_off(&pt), "the prologue was not restored byte-for-byte");
    }

    {
        long differ = 0;
        rec_t got;
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.st != expected[i].st || got.val != expected[i].val) ++differ;
        }
        printf("  [reverted]   %d cases, %ld differ;  our-code calls = %ld (must be 0)\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "the export did not answer as before after the revert");
        OK(c_hit == 0, "our code still ran after the revert");
    }

    if (!failures)
        printf("LIVE SUBSTITUTION: PASS (the shipped export replaced in a sacrificial child, every case\n"
               "compared on the NTSTATUS and the caller's ULONG including the refusals that must not\n"
               "write it, both invalid-base mechanisms driven, and the prologue restored byte-for-byte)\n");
    else
        printf("LIVE SUBSTITUTION: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
