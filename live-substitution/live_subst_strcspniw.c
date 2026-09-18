// live-substitution/live_subst_strcspniw.c
// LIVE-RUN PROOF for change 285 (shlwapi!StrCSpnIW).
//
// WHAT IS COMPARED IS THE RETURNED COUNT. This export returns an int, not a pointer, so there is no
// byte-versus-code-unit trap here -- but the count must be exact, including past 16 bits.
//
// THE CONTRACT THIS HARNESS RESPECTS, from changes/285-strcspniw/probes/contract.c and
// probes/relation.c:
//
//   * the answer is the index of the first character that is in the set, or the length if none;
//   * the relation is change 281's EXACTLY -- extracted over 786420 pairs with zero disagreements,
//     symmetric, and a multi-member set is exactly the UNION of its members' rows;
//   * an embedded NUL ends the scan; an empty set gives the length; an empty string and any NULL
//     argument give 0;
//   * and the virtual NUL that changes 283 and 284 had to model is UNOBSERVABLE here: whether the
//     terminator counts as a member or merely stops the scan, the answer is the length either way.
//
// THE FORCED SUB-CASES ARE CHOSEN FROM WHAT THIS CHANGE'S OWN STRUCTURE CAN GET WRONG, because that is
// where the two previous changes' harnesses were weakest:
//
//   * cur_one    -- a set expanding to at most four accept entries, which takes a single unbounded
//                   pass and skips the windowing entirely;
//   * cur_multi  -- a set expanding past four, which exercises the DOUBLING WINDOWS and the bound that
//                   tightens as chunks find better answers;
//   * cur_big    -- a set holding a 255-sentinel member, which forces the call-free scalar path;
//   * cur_mixed  -- a 255-sentinel member alongside ordinary ones, so the scalar path's three loops
//                   (self, pool, bitmap) all run in one call;
//   * cur_guard  -- the terminator as the last readable code unit before an unmapped page, which is
//                   the only placement that tests where the scan really stops.
//
// Each carries an assertion that fails if the draw stops producing it.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG state
// across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL: sacrificial single-threaded child; validate first; patch only when idle;
// restore and verify the prologue byte-for-byte, then re-run the whole corpus through it.
//
// Build: build_strcspniw_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef int (WINAPI *FSPN)(PCWSTR, PCWSTR);

extern int wia_strcspniw(const wchar_t*, const wchar_t*);
int wia_sci_init(void);
int wia_sci_match(unsigned needle, unsigned w);
extern unsigned char wia_sci_n[65536];

static volatile LONG c_hit;
static int WINAPI w_spn(PCWSTR s, PCWSTR set)
{ _InterlockedIncrement(&c_hit); return wia_strcspniw(s, set); }

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

static int expected[NCASE];
static wchar_t buf[SBUF + 64];
static wchar_t setb[40];
static unsigned char* gbase;
static SIZE_T gpg;

static const wchar_t* cur_s;
static const wchar_t* cur_set;
static int cur_guard, cur_one, cur_multi, cur_big, cur_mixed, cur_embnul, cur_empty;
static unsigned big_member, mid_member;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    unsigned n, k, start, nset;
    int kind = (int)(i % 5);        /* 0,1 one-member-ish; 2 multi; 3 big; 4 mixed */

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    n = 1 + rnd() % SBUF;
    start = 32 + rnd() % 32;
    /* a SMALL alphabet, so a match is common and the span usually stops early */
    for (k = 0; k < n; ++k) buf[start + k] = (wchar_t)(L'a' + (rnd() % 6));
    cur_embnul = 0;
    if ((i % 9) == 4 && n > 3) { buf[start + (rnd() % n)] = 0; cur_embnul = 1; }
    buf[start + n] = 0;
    cur_s = buf + start;
    cur_guard = 0;

    if (gbase && (i % 3) == 1) {
        wchar_t* g = (wchar_t*)(gbase + gpg) - (n + 1);
        for (k = 0; k <= n; ++k) g[k] = buf[start + k];
        cur_s = g;
        cur_guard = 1;
    }

    cur_one = cur_multi = cur_big = cur_mixed = cur_empty = 0;
    if (kind <= 1) {
        /* one or two members drawn from the alphabet and just outside it, so the answer lands both
           early and at the length. At most eight accept entries, so at most two chunks. */
        nset = 1 + (rnd() & 1);
        for (k = 0; k < nset; ++k) setb[k] = (wchar_t)(L'A' + (rnd() % 8));
        setb[nset] = 0;
        cur_one = 1;
    } else if (kind == 2) {
        /* many members: the doubling windows and several chunks */
        nset = 5 + rnd() % 14;
        for (k = 0; k < nset; ++k) setb[k] = (wchar_t)(L'A' + (rnd() % 20));
        setb[nset] = 0;
        cur_multi = 1;
    } else if (kind == 3) {
        /* A 255-sentinel member alone: the scalar path, bitmap loop only.
         *
         * HALF OF THESE USE A SENTINEL THAT DOES NOT ACCEPT A NUL. There are eleven distinct sentinel
         * sets and only one -- the 3238-member ignorable set -- contains a NUL. Using only that one
         * leaves the scalar loop's own terminator test unnecessary, because the bitmap stops the loop
         * for free; a mutant removing that test then survives this gate, as one did. U+D7A2 carries
         * the sentinel with 238 members and does NOT accept a NUL. */
        setb[0] = (wchar_t)((i & 1) ? big_member : 0xD7A2);
        setb[1] = 0;
        cur_big = 1;
        /* and sometimes plant one of its 3237 members in the string, so it really matches */
        if ((rnd() & 1) && n > 2) buf[start + (rnd() % n)] = 0x034F;
        if (cur_guard) {
            wchar_t* g = (wchar_t*)(gbase + gpg) - (n + 1);
            for (k = 0; k <= n; ++k) g[k] = buf[start + k];
        }
    } else {
        /* mixed: a sentinel member, a no-partner member and a 5..8-partner member, so all three
           scalar loops run inside one call */
        setb[0] = (wchar_t)(L'A' + (rnd() % 6));
        setb[1] = (wchar_t)big_member;
        setb[2] = (wchar_t)mid_member;
        setb[3] = 0xFFFD;
        setb[4] = 0;
        cur_mixed = 1;
    }
    if ((i % 37) == 11) { setb[0] = 0; cur_empty = 1; cur_one = cur_multi = cur_big = cur_mixed = 0; }
    cur_set = setb;
}

static int run_case(FSPN f) { return f(cur_s, cur_set); }

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    FSPN live = hs ? (FSPN)GetProcAddress(hs, "StrCSpnIW") : 0;
    patch_t pt;
    SYSTEM_INFO si;
    long i;
    long n_early = 0, n_full = 0, n_guard = 0, n_one = 0, n_multi = 0, n_big = 0, n_mixed = 0;
    long n_embnul = 0, n_empty = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("change 281's tables disagree with the live export\n"); return 1; }
    printf("== LIVE SUBSTITUTION: shlwapi!StrCSpnIW (change 285) ==\n");

    GetSystemInfo(&si);
    gpg = si.dwPageSize;
    gbase = (unsigned char*)VirtualAlloc(0, gpg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!gbase || !VirtualAlloc(gbase, gpg, MEM_COMMIT, PAGE_READWRITE)) { printf("  guard\n"); return 1; }

    for (i = 1; i < 0xFFFF; ++i) if (wia_sci_n[i] == 255) { big_member = (unsigned)i; break; }
    for (i = 1; i < 0xFFFF; ++i)
        if (wia_sci_n[i] >= 5 && wia_sci_n[i] <= 8) { mid_member = (unsigned)i; break; }
    printf("  the scalar path is forced by U+%04X (the 255 bitmap sentinel, %d accepted code units)\n"
           "  and its pool loop by U+%04X (%d partners), one case in five each\n",
           big_member, 3237, mid_member, (int)wia_sci_n[mid_member]);

    for (i = 0; i < NCASE; ++i) {
        int len = 0;
        build_case(i);
        expected[i] = run_case(live);
        while (cur_s[len]) ++len;
        if (expected[i] < len) ++n_early; else ++n_full;
        if (cur_guard) ++n_guard;
        if (cur_one) ++n_one;
        if (cur_multi) ++n_multi;
        if (cur_big) ++n_big;
        if (cur_mixed) ++n_mixed;
        if (cur_embnul) ++n_embnul;
        if (cur_empty) ++n_empty;
    }
    printf("  [pre-patch]  %d cases;  %ld stopped early, %ld ran to the end\n"
           "               pinned to a guard page %ld,  embedded NULs %ld,  empty sets %ld\n"
           "               single-pass sets %ld,  windowed multi-chunk sets %ld,\n"
           "               scalar-path sets %ld,  mixed scalar sets %ld\n",
           NCASE, n_early, n_full, n_guard, n_embnul, n_empty, n_one, n_multi, n_big, n_mixed);
    OK(n_early > 5000, "the corpus rarely found anything in the set");
    OK(n_full  > 2000, "the corpus rarely ran to the end, which is the path that scans everything");
    OK(n_guard > 7000, "the corpus barely used the guard page, which is where the scan must stop");
    OK(n_embnul > 1500, "the corpus barely used an embedded NUL, which is what ends the scan");
    OK(n_empty > 400,  "the corpus barely used an empty set");
    OK(n_one   > 5000, "the corpus barely used a set small enough to take the single unbounded pass");
    OK(n_multi > 3000, "the corpus barely used a set big enough to exercise the doubling windows");
    OK(n_big   > 3000, "the corpus barely used a 255-sentinel set, so the scalar path went untested");
    OK(n_mixed > 3000, "the corpus barely mixed member kinds, so the scalar path's three loops did\n"
                       "        not all run together");

    {
        long differ = 0;
        if (!patch_on(&pt, (void*)live, (void*)w_spn)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            int got;
            build_case(i);
            got = run_case(live);
            if (got != expected[i]) {
                if (++differ <= 8)
                    printf("  differ at %ld (set len %d): live %d  ours %d\n",
                           i, (int)wcslen(cur_set), expected[i], got);
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
    printf("\nLIVE SUBSTITUTION: PASS (the returned COUNT identical over %d cases, with the single\n"
           "unbounded pass, the doubling windows, the call-free scalar path and its three loops,\n"
           "embedded NULs, empty sets and a guard page driven; prologue restored byte-exact)\n", NCASE);
    return 0;
}
