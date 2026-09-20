// live-substitution/live_subst_strstriw.c
// LIVE-RUN PROOF for change 284 (shlwapi!StrStrIW).
//
// What is compared is the returned pointer as a byte offset. Change 282 found a mutant that
// returned a pointer one byte into the middle of a wchar_t and survived both gates, because
// `p - base` on a wchar_t* divides the odd byte away.
//
// The contract this harness has to respect, all measured by changes/284-strstriw/probes/contract.c:
//
//   * the FIRST match is returned, so a corpus must plant more than one and care which comes back;
//   * the haystack is NUL-terminated and an EMBEDDED NUL ends the search;
//   * past the terminator the string behaves as an endless run of NULs, and those NULs are never
//     Loaded: with 'W' after the terminator, {q,shy,shy} is found and {q,w} is not. 3320 code units
//     match a NUL, so a needle whose TAIL matches a NUL can match ACROSS the end, and that is the
//     only shape in which the candidate bound is observable at all;
//   * a needle LONGER than the whole string can therefore match;
//   * a match may START only at a real character: the terminator is not a candidate position;
//   * an EMPTY needle returns NULL, the opposite of C strstr.
//
// The sub-cases below are not a guess at what might matter. Change 283's harness passed a build with
// a wrong candidate bound, and passed mutants that dropped the empty-needle refusal and moved the
// WIDE threshold, because its draw could not express those cases. Each of the three forced sub-cases
// here closes one of those holes, and each has an assertion that fails if the draw stops producing
// it:
//
//   * cur_nultail, a needle whose tail matches a NUL, over a string whose last character matches
//     the needle's first, so the match exists only by running past the terminator;
//   * cur_mid; a needle whose first character has a REAL partner count above four (U+004B has
//     five), matched through the HIGHEST member of its set. U+00AD reaches the WIDE path through a
//     255 sentinel instead, which leaves the threshold itself untested;
//   * cur_nulhay; a haystack holding a NUL-matching code unit, so that an EMPTY needle has
//     something to find; without it the unguarded path finds nothing and agrees for the wrong reason.
//
// And one that is specific to a FORWARD search: cur_two plants a SECOND match above the first, so
// returning the wrong one is visible.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL: sacrificial single-threaded child; validate first; patch only when idle;
// restore and verify the prologue byte-for-byte, then re-run the whole corpus through it.
//
// Build: build_strstriw_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef PCWSTR (WINAPI *F2)(PCWSTR, PCWSTR);

extern const wchar_t* wia_strstriw(const wchar_t*, const wchar_t*);
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern unsigned char wia_sci_n[65536];

static volatile LONG c_hit;
static PCWSTR WINAPI w_sstr(PCWSTR s, PCWSTR n)
{ _InterlockedIncrement(&c_hit); return (PCWSTR)wia_strstriw(s, n); }

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
static wchar_t ned[20];
static unsigned char* gbase;
static SIZE_T gpg;

static const wchar_t* cur_s;
static const wchar_t* cur_n;
static int cur_guard, cur_wide, cur_mid, cur_nultail, cur_nulhay, cur_two, cur_embnul;
static unsigned wide_needle, mid_needle, mid_last;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    unsigned n, k, start, nl;
    int want_nultail = ((i % 11) == 3);
    int want_mid     = ((i % 13) == 6) && !want_nultail;
    int want_nulhay  = ((i % 17) == 8);
    int want_two     = ((i % 5) == 2);

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    n = 1 + rnd() % SBUF;
    start = 32 + rnd() % 32;
    /* a SMALL alphabet, so matches are common and the last-character reject is exercised */
    for (k = 0; k < n; ++k) buf[start + k] = (wchar_t)(L'a' + (rnd() % 6));
    /* embedded NULs sometimes: the NUL ends the search and that must be reproduced */
    cur_embnul = 0;
    if ((i % 9) == 4 && n > 3) { buf[start + (rnd() % n)] = 0; cur_embnul = 1; }
    /* the forced sub-cases touch the haystack, so they happen before the guard-page copy */
    if (want_nulhay) buf[start + (rnd() % n)] = 0x00AD;
    if (want_nultail) buf[start + n - 1] = L'q';
    if (want_mid && n > 2) {
        buf[start + n - 2] = (wchar_t)mid_last;
        buf[start + n - 1] = L'a';
    }
    buf[start + n] = 0;
    cur_s = buf + start;
    cur_guard = 0;

    if (gbase && (i % 3) == 1) {
        /* the terminator as the last readable code unit before the guard page */
        wchar_t* g = (wchar_t*)(gbase + gpg) - (n + 1);
        for (k = 0; k <= n; ++k) g[k] = buf[start + k];
        cur_s = g;
        cur_guard = 1;
    }

    nl = 1 + rnd() % 5;
    cur_wide = 0; cur_mid = 0; cur_nultail = 0; cur_nulhay = want_nulhay; cur_two = 0;
    if (want_nultail) {                     /* a tail that matches the terminator */
        nl = 2 + (rnd() % 3);
        /* Half of these make the whole needle NUL-matching, not just its tail. It matters: with a
           first character that does not match a NUL, maxtail is at most nlen-1 and the cap that
           stops region B at the last real character never binds -- so a mutant removing that cap
           survived this gate while gate 1 caught it. Only maxtail == nlen exercises it. */
        ned[0] = ((i & 1) ? (wchar_t)0x00AD : L'Q');
        for (k = 1; k < nl; ++k) ned[k] = 0x00AD;
        cur_nultail = 1;
    } else if (want_mid && n > 2) {         /* five to eight partners, via a later set member */
        nl = 2;
        ned[0] = (wchar_t)mid_needle;
        ned[1] = L'A';
        cur_mid = 1;
    } else if ((i % 7) == 2) {              /* force the WIDE filter path */
        ned[0] = (wchar_t)wide_needle;
        cur_wide = 1;
        for (k = 1; k < nl; ++k) ned[k] = (wchar_t)(L'a' + (rnd() % 6));
    } else {
        for (k = 0; k < nl; ++k) ned[k] = (wchar_t)(L'A' + (rnd() % 6));
        /* FIRST-MATCH DISCIPLINE: plant the needle twice, well apart, so returning the later one
           is visible. Only for the plain draw, where the needle is ordinary letters. */
        if (want_two && nl >= 2 && n > 3 * nl + 8 && !cur_guard) {
            unsigned a = 1 + rnd() % (n / 3);
            unsigned b = a + nl + 2 + rnd() % (n / 3);
            if (b + nl < n) {
                for (k = 0; k < nl; ++k) {
                    buf[start + a + k] = (wchar_t)(ned[k] + 32);
                    buf[start + b + k] = (wchar_t)(ned[k] + 32);
                }
                cur_two = 1;
            }
        }
    }
    ned[nl] = 0;
    if ((i % 31) == 5) ned[0] = 0;          /* an empty needle */
    cur_n = ned;
}

static long run_case(F2 f)
{
    PCWSTR p = f(cur_s, cur_n);
    return p ? (long)((const char*)p - (const char*)cur_s) : -1;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    F2 live = hs ? (F2)GetProcAddress(hs, "StrStrIW") : 0;
    patch_t pt;
    SYSTEM_INFO si;
    long i;
    long n_hit = 0, n_miss = 0, n_guard = 0, n_wide = 0, n_nul = 0, n_empty = 0;
    long n_nultail = 0, n_mid = 0, n_emptynul = 0, n_two = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }
    printf("== LIVE SUBSTITUTION: shlwapi!StrStrIW (change 284) ==\n");

    GetSystemInfo(&si);
    gpg = si.dwPageSize;
    gbase = (unsigned char*)VirtualAlloc(0, gpg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!gbase || !VirtualAlloc(gbase, gpg, MEM_COMMIT, PAGE_READWRITE)) { printf("  guard\n"); return 1; }

    for (i = 1; i < 0xFFFF; ++i) if (wia_sci_n[i] == 255) { wide_needle = (unsigned)i; break; }
    printf("  the WIDE filter path (a needle whose first character has more than four partners,\n"
           "  which bypasses the vector scan) is forced by U+%04X, one case in seven\n", wide_needle);

    /* U+00AD reaches the WIDE path through a 255 SENTINEL, not a real count, so it leaves the actual
       threshold untested. The only real counts above four are 5, 6, 7 and 8. */
    for (i = 1; i < 0xFFFF; ++i)
        if (wia_sci_n[i] >= 5 && wia_sci_n[i] <= 8) { mid_needle = (unsigned)i; break; }
    for (i = 1; i < 0xFFFF; ++i)
        if (wia_sci_match(mid_needle, (unsigned)i)) mid_last = (unsigned)i;
    printf("  the WIDE THRESHOLD itself is driven by U+%04X (%d partners), matched through the\n"
           "  highest member of its set, U+%04X, one case in thirteen\n",
           mid_needle, (int)wia_sci_n[mid_needle], mid_last);

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        expected[i] = run_case(live);
        if (expected[i] >= 0) ++n_hit; else ++n_miss;
        if (cur_guard) ++n_guard;
        if (cur_wide) ++n_wide;
        if (!cur_n[0]) ++n_empty;
        if (cur_nultail) ++n_nultail;
        if (cur_mid) ++n_mid;
        if (cur_two) ++n_two;
        if (!cur_n[0] && cur_nulhay) ++n_emptynul;
        if (cur_embnul) ++n_nul;         /* an EMBEDDED NUL, which ends the search early */
    }
    printf("  [pre-patch]  %d cases;  %ld hits, %ld misses\n"
           "               pinned to a guard page %ld,  WIDE-filter needles %ld,\n"
           "               embedded NULs %ld,  empty needles %ld\n"
           "               NUL-matching needle tails %ld,  WIDE-threshold needles %ld,\n"
           "               empty needle over a NUL-matching haystack %ld,  two planted matches %ld\n",
           NCASE, n_hit, n_miss, n_guard, n_wide, n_nul, n_empty,
           n_nultail, n_mid, n_emptynul, n_two);
    OK(n_hit   > 5000, "the corpus rarely found anything");
    OK(n_miss  > 5000, "the corpus rarely missed, which is the path that tries every position");
    OK(n_guard > 7000, "the corpus barely used the guard page, which is where the scan must stop");
    OK(n_wide  > 2000, "the corpus barely used the WIDE filter path");
    OK(n_nul   > 1500, "the corpus barely used an EMBEDDED NUL, which is what ends the search");
    OK(n_empty > 400,  "the corpus barely used an empty needle");
    OK(n_nultail > 1500, "the corpus barely used a needle whose tail matches a NUL -- the only\n"
                         "        shape in which the candidate bound is observable at all");
    OK(n_mid > 1000, "the corpus barely used a needle whose first character has a REAL partner\n"
                     "        count above four, so the WIDE threshold itself went untested");
    OK(n_emptynul > 20, "the corpus never combined an empty needle with a haystack holding a\n"
                        "        NUL-matching code unit, so the empty-needle refusal went untested");
    OK(n_two > 1500, "the corpus barely planted TWO matches, so first-match discipline -- the one\n"
                     "        thing that separates this export from StrRStrIW -- went untested");

    {
        long differ = 0;
        if (!patch_on(&pt, (void*)live, (void*)w_sstr)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            long got;
            build_case(i);
            got = run_case(live);
            if (got != expected[i]) {
                if (++differ <= 8)
                    printf("  differ at %ld (nlen %d): live %ld  ours %ld\n",
                           i, (int)wcslen(cur_n), expected[i], got);
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
            long got;
            build_case(i);
            got = run_case(live);
            if (got != expected[i]) ++differ;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  our-code calls = "
               "%ld (must not have moved)\n", NCASE, differ, (long)c_hit);
        OK(differ == 0, "the restored export does not agree with itself");
        OK(c_hit == 0, "our code still ran after the patch was removed");
    }

    if (failures) { printf("\nLIVE SUBSTITUTION: FAIL (%d)\n", failures); return 1; }
    printf("\nLIVE SUBSTITUTION: PASS (the returned BYTE offset identical over %d cases, with both\n"
           "filter paths, first-match discipline, embedded NULs, needle tails that match a NUL, the\n"
           "WIDE threshold itself, empty needles and a guard page driven; prologue restored\n"
           "byte-exact)\n", NCASE);
    return 0;
}
