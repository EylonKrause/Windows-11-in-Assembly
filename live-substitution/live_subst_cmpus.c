// live-substitution/live_subst_cmpus.c
// LIVE-RUN PROOF for change 263 (ntdll!RtlCompareUnicodeStrings).
//
// The exact long is compared, not its sign. probes/contract.c showed the export returns the
// DIFFERENCE of the two characters, -25 for `A` against `Z`, 65535 for U+FFFF against U+0000 --
// so an implementation returning -1/0/1 would satisfy every caller that writes `< 0` and every
// check that only looked at the sign.
//
// The corpus is built to produce all three answers, and the run reports how many of each it got:
// equal, decided by a character, and decided by the LENGTHS. A corpus of random strings answers
// "different at the first character" almost every time, which would exercise neither the vector
// loop that scans to the end nor the length tie-break.
//
// And it is built to reach both case-insensitive paths. Our implementation folds a disagreeing
// block in-vector when every character in it is ASCII and goes through the upcase table otherwise,
// so a corpus of pure ASCII would leave the table path completely untested while looking thorough.
// One case in four is drawn from a Latin-1 and beyond alphabet for that reason, and the run counts
// how many cases contained a character at or above 0x80.
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
// Build: build_cmpus_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG (NTAPI *F_Cmp)(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);

extern LONG wia_compareunicodestrings(const wchar_t*, SIZE_T, const wchar_t*, SIZE_T, BOOLEAN);
extern unsigned short wia_upcase[65536];
void wia_upcase_init(void);

static volatile LONG c_hit;
static LONG NTAPI w_cmp(const wchar_t* a, SIZE_T la, const wchar_t* b, SIZE_T lb, BOOLEAN ci)
{ _InterlockedIncrement(&c_hit); return wia_compareunicodestrings(a, la, b, lb, ci); }

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
#define MAXCH 600
static wchar_t sa[MAXCH], sb[MAXCH];
static SIZE_T cur_la, cur_lb;
static int cur_ci, cur_high;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    int shape = (int)(i % 8), k;
    int high = (int)(i % 4) == 0;               /* one case in four leaves ASCII behind */
    rs = 0x452821E638D01377ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    cur_la = (SIZE_T)(rnd() % 300);
    cur_lb = cur_la;
    cur_ci = (int)(rnd() & 1);
    cur_high = high;

    for (k = 0; k < MAXCH; ++k) {
        unsigned r = rnd();
        sa[k] = high ? (wchar_t)(0x00C0 + (r % 0x180))   /* Latin-1 and Latin Extended-A */
                     : (wchar_t)(0x61 + (r % 26));
    }
    for (k = 0; k < MAXCH; ++k) {
        wchar_t c = sa[k];
        switch (shape) {
        case 0: sb[k] = c; break;                                   /* identical */
        case 1:                                                     /* differs only in case */
        case 2: sb[k] = (wchar_t)wia_upcase[c]; break;
        case 3: sb[k] = (c >= 0x61 && c <= 0x7A) ? (wchar_t)(c - 32) : c; break;
        case 4: sb[k] = (wchar_t)(c + 1); break;                    /* differs everywhere */
        case 5: sb[k] = ((k % 97) == 40) ? (wchar_t)(c ^ 1) : c; break;  /* one difference, deep */
        case 6: sb[k] = (k == 0) ? (wchar_t)(c ^ 4) : c; break;     /* differs at the very front */
        default: sb[k] = (wchar_t)rnd(); break;
        }
    }
    if (shape == 7 || (rnd() & 7) == 0) cur_lb = (SIZE_T)(rnd() % 300);  /* unequal lengths */
}

static LONG exp_r[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Cmp live = (F_Cmp)GetProcAddress(h, "RtlCompareUnicodeStrings");
    patch_t p;
    long i, eq = 0, bychar = 0, bylen = 0, nhigh = 0, nci = 0;

    if (!live) { printf("resolve failed\n"); return 1; }
    wia_upcase_init();
    printf("== LIVE SUBSTITUTION: ntdll!RtlCompareUnicodeStrings (change 263) ==\n");

    for (i = 0; i < NCASE; ++i) {
        LONG r;
        build_case(i);
        r = live(sa, cur_la, sb, cur_lb, (BOOLEAN)cur_ci);
        exp_r[i] = r;
        if (r == 0) ++eq;
        else if (cur_la == cur_lb) ++bychar;
        else {
            /* decided by the lengths only if the common prefix agreed all the way */
            SIZE_T n = cur_la < cur_lb ? cur_la : cur_lb, k;
            int same = 1;
            for (k = 0; k < n; ++k) {
                unsigned x = sa[k], y = sb[k];
                if (cur_ci) { x = wia_upcase[x]; y = wia_upcase[y]; }
                if (x != y) { same = 0; break; }
            }
            if (same) ++bylen; else ++bychar;
        }
        if (cur_high) ++nhigh;
        if (cur_ci) ++nci;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED code\n", NCASE);
    printf("               EQUAL %ld, decided by a CHARACTER %ld, decided by the LENGTHS %ld\n",
           eq, bychar, bylen);
    printf("               case-insensitive %ld, containing a character at or above 0x80 %ld\n",
           nci, nhigh);
    OK(eq > 2000,     "the corpus rarely compared equal -- the full scan would be untested");
    OK(bychar > 2000, "the corpus rarely stopped at a character");
    OK(bylen > 500,   "the corpus rarely reached the length tie-break");
    OK(nhigh > 5000,  "the corpus was nearly all ASCII -- the table path would be untested");

    {
        long differ = 0;
        if (!patch_on(&p, (void*)live, (void*)w_cmp)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            if (live(sa, cur_la, sb, cur_lb, (BOOLEAN)cur_ci) != exp_r[i]) ++differ;
        }
        printf("  [patched]    %d cases, %ld differ (the exact LONG);  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "RtlCompareUnicodeStrings answered differently under the patch");
        OK(c_hit == (LONG)NCASE, "the counter did not move once per call");
        OK(patch_off(&p), "the prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG before = c_hit;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            if (live(sa, cur_la, sb, cur_lb, (BOOLEAN)cur_ci) != exp_r[i]) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (the exact LONG on every case, both flags, both\n"
                      "case-insensitive paths, proved by its own counter and restored byte-exact)\n",
           failures);
    return failures ? 1 : 0;
}
