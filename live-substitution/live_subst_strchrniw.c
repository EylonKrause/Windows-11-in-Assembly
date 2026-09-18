// live-substitution/live_subst_strchrniw.c
// LIVE-RUN PROOF for change 286 (shlwapi!StrChrNIW).
//
// WHAT IS COMPARED IS THE RETURNED POINTER AS A BYTE OFFSET. Change 282 found a mutant that returned a
// pointer one byte into the middle of a wchar_t and survived both gates, because `p - base` on a
// wchar_t* divides the odd byte away. Change 285 then found the same edit HARMLESS, because its export
// returns a count that divides it away legitimately -- this one returns a pointer, so the byte-offset
// comparison matters again.
//
// THE CONTRACT THIS HARNESS RESPECTS, from changes/286-strchrniw/probes/contract.c:
//
//   * the prototype is (start, match, count) -- settled by calling the same address through both
//     candidate prototypes, because discovery/charclass_strcmp_2026.c had timed it as a range form and
//     produced a number for a different question;
//   * the count is the number of characters EXAMINED, indices 0 .. cchMax-1;
//   * the relation is change 281's, including the intransitive triple and the ignorable set;
//   * THE TERMINATOR STOPS THE SCAN AND IS NEVER A MATCH, which is where it differs from changes 283
//     and 284 -- so a NUL-matching character must give NULL over a string with no other match, however
//     far the count reaches;
//   * a NULL start or a count of zero gives NULL;
//   * and on an UNTERMINATED string the export faults even when the count covers the buffer, so every
//     string here is terminated.
//
// THE FORCED SUB-CASES, each with an assertion that fails if the draw stops producing it. Two of them
// exist because a mutant walked through an earlier change's harness:
//
//   * cur_short   -- a count that stops BEFORE the match, so the count alone decides the answer;
//   * cur_exact   -- a count that reaches the match by exactly one character;
//   * cur_past    -- a count reaching far past the terminator, so the terminator must stop the scan;
//   * cur_nulchar -- a sought character that MATCHES a NUL, which must still not match the terminator;
//   * cur_wide    -- a character with more than four partners, the call-free scalar loops;
//   * cur_sent    -- half of those use a sentinel set that does NOT accept a NUL, because only one of
//                    the eleven sentinel sets does and using just that one leaves the wide loop's own
//                    terminator test unnecessary (change 285 was caught by exactly that);
//   * cur_guard   -- the terminator as the last readable code unit before an unmapped page.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG state
// across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL: sacrificial single-threaded child; validate first; patch only when idle;
// restore and verify the prologue byte-for-byte, then re-run the whole corpus through it.
//
// Build: build_strchrniw_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef PWSTR (WINAPI *FCN)(PCWSTR, WCHAR, UINT);

extern const wchar_t* wia_strchrniw(const wchar_t*, wchar_t, unsigned);
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern unsigned char wia_sci_n[65536];

static volatile LONG c_hit;
static PWSTR WINAPI w_chrn(PCWSTR s, WCHAR m, UINT n)
{ _InterlockedIncrement(&c_hit); return (PWSTR)wia_strchrniw(s, m, n); }

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
#define SBUF  400

static long expected[NCASE];
static wchar_t buf[SBUF + 64];
static unsigned char* gbase;
static SIZE_T gpg;

static const wchar_t* cur_s;
static wchar_t cur_m;
static unsigned cur_n;
static int cur_guard, cur_short, cur_exact, cur_past, cur_nulchar, cur_wide, cur_sent, cur_embnul;
static unsigned big_nul, big_nonul, mid_member;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    unsigned n, k, start, pos = 0;
    int planted = 0;

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    n = 2 + rnd() % (SBUF - 2);
    start = 32 + rnd() % 32;
    /* U+0002 matches only itself, so it never accidentally matches the sought character -- four
       corpora in this family were once vacuous because their filler did. */
    for (k = 0; k < n; ++k) buf[start + k] = 0x0002;
    cur_embnul = 0;
    if ((i % 11) == 4 && n > 4) { buf[start + 1 + (rnd() % (n - 2))] = 0; cur_embnul = 1; }

    cur_guard = cur_short = cur_exact = cur_past = cur_nulchar = cur_wide = cur_sent = 0;

    /* choose the sought character */
    if ((i % 7) == 3) {
        cur_wide = 1;
        if (i & 1) { cur_m = (wchar_t)big_nul; cur_nulchar = 1; }
        else       { cur_m = (wchar_t)big_nonul; cur_sent = 1; }
    } else if ((i % 7) == 5) {
        cur_wide = 1;
        cur_m = (wchar_t)mid_member;
    } else if ((i % 13) == 6) {
        cur_m = (wchar_t)0x00AD;            /* matches a NUL, and takes the sentinel path */
        cur_nulchar = 1; cur_wide = 1;
    } else {
        cur_m = (wchar_t)(L'A' + (rnd() % 6));
    }

    /* plant a match sometimes, at a known position */
    if ((rnd() & 3) != 0) {
        unsigned mem, seen = 0, want = 1 + rnd() % 4;
        for (mem = 1; mem < 65536; ++mem)
            if (wia_sci_match((unsigned)cur_m, mem)) { if (++seen == want) break; }
        if (mem < 65536 && mem != 0x0002) {
            pos = rnd() % n;
            buf[start + pos] = (wchar_t)mem;
            planted = 1;
        }
    }
    buf[start + n] = 0;
    cur_s = buf + start;

    if (gbase && (i % 3) == 1) {
        wchar_t* g = (wchar_t*)(gbase + gpg) - (n + 1);
        for (k = 0; k <= n; ++k) g[k] = buf[start + k];
        cur_s = g;
        cur_guard = 1;
    }

    /* choose the count, forcing the three interesting relationships to the planted position */
    switch (i % 5) {
    case 0:
        if (planted) { cur_n = pos; cur_short = 1; }        /* stops one short of the match */
        else cur_n = n;
        break;
    case 1:
        if (planted) { cur_n = pos + 1; cur_exact = 1; }    /* reaches it by exactly one */
        else cur_n = n;
        break;
    case 2:
        cur_n = 0xFFFFFFFFu; cur_past = 1;                  /* far past the terminator */
        break;
    case 3:
        cur_n = n;
        break;
    default:
        cur_n = 1 + rnd() % (n + 8);
        if (cur_n > n) cur_past = 1;
        break;
    }
    if ((i % 101) == 7) cur_n = 0;                           /* and a count of zero sometimes */
}

static long run_case(FCN f)
{
    const void* p = f(cur_s, cur_m, cur_n);
    return p ? (long)((const char*)p - (const char*)cur_s) : -1;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    FCN live = hs ? (FCN)GetProcAddress(hs, "StrChrNIW") : 0;
    patch_t pt;
    SYSTEM_INFO si;
    long i;
    long n_hit = 0, n_miss = 0, n_guard = 0, n_short = 0, n_exact = 0, n_past = 0;
    long n_nulchar = 0, n_wide = 0, n_sent = 0, n_embnul = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }
    printf("== LIVE SUBSTITUTION: shlwapi!StrChrNIW (change 286) ==\n");

    GetSystemInfo(&si);
    gpg = si.dwPageSize;
    gbase = (unsigned char*)VirtualAlloc(0, gpg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!gbase || !VirtualAlloc(gbase, gpg, MEM_COMMIT, PAGE_READWRITE)) { printf("  guard\n"); return 1; }

    for (i = 1; i < 0xFFFF; ++i)
        if (wia_sci_n[i] == 255 && wia_sci_match((unsigned)i, 0)) { big_nul = (unsigned)i; break; }
    for (i = 1; i < 0xFFFF; ++i)
        if (wia_sci_n[i] == 255 && !wia_sci_match((unsigned)i, 0)) { big_nonul = (unsigned)i; break; }
    for (i = 1; i < 0xFFFF; ++i)
        if (wia_sci_n[i] >= 5 && wia_sci_n[i] <= 8) { mid_member = (unsigned)i; break; }
    printf("  the wide loops are forced by U+%04X (255 sentinel, DOES accept a NUL), U+%04X (255\n"
           "  sentinel, does NOT -- only one of the eleven sentinel sets accepts one, and using only\n"
           "  that one leaves the loop's own terminator test unnecessary) and U+%04X (%d partners)\n",
           big_nul, big_nonul, mid_member, (int)wia_sci_n[mid_member]);

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        expected[i] = run_case(live);
        if (expected[i] >= 0) ++n_hit; else ++n_miss;
        if (cur_guard) ++n_guard;
        if (cur_short) ++n_short;
        if (cur_exact) ++n_exact;
        if (cur_past) ++n_past;
        if (cur_nulchar) ++n_nulchar;
        if (cur_wide) ++n_wide;
        if (cur_sent) ++n_sent;
        if (cur_embnul) ++n_embnul;
    }
    printf("  [pre-patch]  %d cases;  %ld hits, %ld misses\n"
           "               pinned to a guard page %ld,  embedded NULs %ld\n"
           "               count one SHORT of the match %ld,  count EXACTLY reaching it %ld,\n"
           "               count far PAST the terminator %ld\n"
           "               NUL-matching sought characters %ld,  wide-path characters %ld,\n"
           "               sentinel sets that do NOT accept a NUL %ld\n",
           NCASE, n_hit, n_miss, n_guard, n_embnul, n_short, n_exact, n_past,
           n_nulchar, n_wide, n_sent);
    OK(n_hit   > 5000, "the corpus rarely found anything");
    OK(n_miss  > 5000, "the corpus rarely missed, which is the path that scans everything");
    OK(n_guard > 7000, "the corpus barely used the guard page, which is where the scan must stop");
    OK(n_short > 2000, "the corpus barely used a count that stops before the match -- the one thing\n"
                       "        that separates this export from StrChrIW");
    OK(n_exact > 2000, "the corpus barely used a count that reaches the match by exactly one");
    OK(n_past  > 4000, "the corpus barely used a count past the terminator, so the terminator's own\n"
                       "        stopping role went untested");
    OK(n_nulchar > 1500, "the corpus barely sought a NUL-matching character, which must still not\n"
                         "        match the terminator");
    OK(n_wide  > 4000, "the corpus barely used the wide path");
    OK(n_sent  > 1000, "the corpus barely used a sentinel set that does NOT accept a NUL");
    OK(n_embnul > 1500, "the corpus barely used an embedded NUL");

    {
        long differ = 0;
        if (!patch_on(&pt, (void*)live, (void*)w_chrn)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            long got;
            build_case(i);
            got = run_case(live);
            if (got != expected[i]) {
                if (++differ <= 8)
                    printf("  differ at %ld (U+%04X count %u): live %ld  ours %ld\n",
                           i, (unsigned)cur_m, cur_n, expected[i], got);
                else ++differ;
            }
        }
        printf("  [patched]    %d cases, %ld differ;  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "the patched export did not agree with the shipped one");
        OK(c_hit == NCASE, "our code was not the one that ran");
        if (!patch_off(&pt)) { printf("  FAIL: the prologue was not restored byte-for-byte\n"); ++failures; }
    }

    {
        long differ = 0;
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            if (run_case(live) != expected[i]) ++differ;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  our-code calls = "
               "%ld (must not have moved)\n", NCASE, differ, (long)c_hit);
        OK(differ == 0, "the restored export does not agree with itself");
        OK(c_hit == 0, "our code still ran after the patch was removed");
    }

    if (failures) { printf("\nLIVE SUBSTITUTION: FAIL (%d)\n", failures); return 1; }
    printf("\nLIVE SUBSTITUTION: PASS (the returned BYTE offset identical over %d cases, with the count\n"
           "one short of the match, exactly reaching it and far past the terminator, NUL-matching sought\n"
           "characters, both wide loops, a sentinel set that does not accept a NUL, embedded NULs and a\n"
           "guard page driven; prologue restored byte-exact)\n", NCASE);
    return 0;
}
