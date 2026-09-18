// live-substitution/live_subst_strrstriw.c
// LIVE-RUN PROOF for change 283 (shlwapi!StrRStrIW).
//
// WHAT IS COMPARED IS THE RETURNED POINTER AS A BYTE OFFSET. Change 282 found a mutant that
// returned a pointer one byte into the middle of a wchar_t and survived both gates, because
// `p - base` on a wchar_t* divides the odd byte away.
//
// THE CONTRACT THIS HARNESS HAS TO RESPECT, all measured by probes/bounds.c:
//
//   * `end` bounds only where a match may START, exclusively -- a match beginning below `end` is
//     returned even though it runs past it;
//   * THE HAYSTACK IS NUL-TERMINATED and the terminator beats `end`: a NUL at index 4 hides a match
//     at 9 however far `end` reaches;
//   * and the export READS TO THE TERMINATOR REGARDLESS OF `end`. With no terminator it FAULTS. So
//     every haystack here is terminated, and the guard-page cases put that terminator as the last
//     readable code unit -- which is the only placement that tests where the scan really stops.
//
// AND THE NEEDLES ARE DRAWN TO REACH BOTH FILTER PATHS. The vector filter keys on the needle's
// FIRST character; a first character with more than four partners bypasses the filter entirely and
// verifies every position. That path is 3321 needles out of 65536, so it is forced rather than left
// to a uniform draw.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG
// state across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL: sacrificial single-threaded child; validate first; patch only when idle;
// restore and verify the prologue byte-for-byte, then re-run the whole corpus through it.
//
// Build: build_strrstriw_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);

extern const wchar_t* wia_strrstriw(const wchar_t*, const wchar_t*, const wchar_t*);
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern unsigned char wia_sci_n[65536];

static volatile LONG c_hit;
static PCWSTR WINAPI w_rstr(PCWSTR s, PCWSTR e, PCWSTR n)
{ _InterlockedIncrement(&c_hit); return (PCWSTR)wia_strrstriw(s, e, n); }

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
static const wchar_t* cur_e;
static const wchar_t* cur_n;
static int cur_guard, cur_wide;
static unsigned wide_needle;

/* THREE SUB-CASES ADDED AFTER MUTATION TESTING, each because a real defect survived this gate.
 *
 *   cur_nultail  a needle whose TAIL matches a NUL, over a string whose last character matches the
 *                needle's first. The live export matches such a needle ACROSS the terminator, so
 *                this is the only shape in which the candidate clamp is observable -- the gate
 *                passed a build that clamped by the needle length, which is wrong.
 *   cur_mid      a needle whose first character has between five and eight partners, matched through
 *                a LATER member of its set. The WIDE dispatch was previously driven only by the
 *                ignorable set, which change 281 stores behind a count of 255, so a mutant that
 *                moved the threshold from 4 to 200 changed nothing and survived.
 *   cur_nulhay   a haystack containing a code unit that matches a NUL, so that an EMPTY needle has
 *                something to find. Without it the unguarded empty-needle path finds nothing and
 *                agrees for the wrong reason.
 */
static int cur_mid, cur_nultail, cur_nulhay;
static unsigned mid_needle, mid_last;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    unsigned n, k, start, nl;
    int want_nultail = ((i % 11) == 3);
    int want_mid     = ((i % 13) == 6) && !want_nultail;
    int want_nulhay  = ((i % 17) == 8);
    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    n = 1 + rnd() % SBUF;
    start = 32 + rnd() % 32;
    /* a SMALL alphabet, so matches are common and the last-character reject is exercised */
    for (k = 0; k < n; ++k) buf[start + k] = (wchar_t)(L'a' + (rnd() % 6));
    /* embedded NULs sometimes: the terminator beats `end` and that must be reproduced */
    if ((i % 9) == 4 && n > 3) buf[start + (rnd() % n)] = 0;
    /* the three sub-cases touch the haystack, so they happen before the guard-page copy below */
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

    /* `end` from inside the string to well past it, since it bounds only the match START */
    cur_e = cur_s + (rnd() % (n + 8));
    if (cur_e <= cur_s) cur_e = cur_s + 1;

    nl = 1 + rnd() % 5;
    cur_wide = 0; cur_mid = 0; cur_nultail = 0; cur_nulhay = want_nulhay;
    if (want_nultail) {                     /* a tail that matches the terminator */
        nl = 2 + (rnd() % 3);
        ned[0] = L'Q';
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
    }
    ned[nl] = 0;
    if ((i % 31) == 5) ned[0] = 0;          /* an empty needle */
    cur_n = ned;
}

static long run_case(F3 f)
{
    PCWSTR p = f(cur_s, cur_e, cur_n);
    return p ? (long)((const char*)p - (const char*)cur_s) : -1;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    F3 live = hs ? (F3)GetProcAddress(hs, "StrRStrIW") : 0;
    patch_t pt;
    SYSTEM_INFO si;
    long i;
    long n_hit = 0, n_miss = 0, n_guard = 0, n_wide = 0, n_nul = 0, n_empty = 0;
    long n_nultail = 0, n_mid = 0, n_emptynul = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }
    printf("== LIVE SUBSTITUTION: shlwapi!StrRStrIW (change 283) ==\n");

    GetSystemInfo(&si);
    gpg = si.dwPageSize;
    gbase = (unsigned char*)VirtualAlloc(0, gpg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!gbase || !VirtualAlloc(gbase, gpg, MEM_COMMIT, PAGE_READWRITE)) { printf("  guard\n"); return 1; }

    for (i = 1; i < 0xFFFF; ++i) if (wia_sci_n[i] == 255) { wide_needle = (unsigned)i; break; }
    printf("  the WIDE filter path (a needle whose first character has more than four partners,\n"
           "  which bypasses the vector scan) is forced by U+%04X, one case in seven\n", wide_needle);

    /* U+00AD reaches the WIDE path through a 255 SENTINEL, not through a real count, so it leaves
       the actual threshold untested. probes/partners.c measured that the only real counts above
       four are 5, 6, 7 and 8; this picks the first such code unit and the HIGHEST member of its
       set, which a four-register filter must drop. */
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
        if (!cur_n[0] && cur_nulhay) ++n_emptynul;
        {
            const wchar_t* q;
            for (q = cur_s; *q; ++q) { }
            if (q < cur_e) ++n_nul;          /* a terminator before `end`: the NUL beats it */
        }
    }
    printf("  [pre-patch]  %d cases;  %ld hits, %ld misses\n"
           "               pinned to a guard page %ld,  WIDE-filter needles %ld,\n"
           "               terminator before `end` %ld,  empty needles %ld\n"
           "               NUL-matching needle tails %ld,  WIDE-threshold needles %ld,\n"
           "               empty needle over a NUL-matching haystack %ld\n",
           NCASE, n_hit, n_miss, n_guard, n_wide, n_nul, n_empty,
           n_nultail, n_mid, n_emptynul);
    OK(n_hit   > 5000, "the corpus rarely found anything");
    OK(n_miss  > 5000, "the corpus rarely missed, which is the path that tries every position");
    OK(n_guard > 7000, "the corpus barely used the guard page, which is where the scan must stop");
    OK(n_wide  > 2000, "the corpus barely used the WIDE filter path");
    OK(n_nul   > 2000, "the corpus barely had a terminator before `end` -- the case where the NUL\n"
                       "        beats the range, which is this export's rule and not StrRChrIW's");
    OK(n_empty > 400,  "the corpus barely used an empty needle");
    OK(n_nultail > 1500, "the corpus barely used a needle whose tail matches a NUL -- the only\n"
                         "        shape in which the candidate clamp is observable at all");
    OK(n_mid > 1000, "the corpus barely used a needle whose first character has a REAL partner\n"
                     "        count above four, so the WIDE threshold itself went untested");
    OK(n_emptynul > 20, "the corpus never combined an empty needle with a haystack holding a\n"
                        "        NUL-matching code unit, so the empty-needle refusal went untested");

    {
        long differ = 0;
        if (!patch_on(&pt, (void*)live, (void*)w_rstr)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            long got;
            build_case(i);
            got = run_case(live);
            if (got != expected[i]) {
                if (++differ <= 8)
                    printf("  differ at %ld (nlen %d, end +%d): live %ld  ours %ld\n",
                           i, (int)wcslen(cur_n), (int)(cur_e - cur_s), expected[i], got);
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
        printf("\nLIVE SUBSTITUTION: PASS (the returned BYTE offset identical over %d cases, with\n"
               "both filter paths, `end` inside and past the string, terminators before `end`,\n"
               "empty needles and a guard page driven; prologue restored byte-exact)\n", NCASE);
    return failures ? 1 : 0;
}
