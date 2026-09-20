// live-substitution/live_subst_charbuff.c
// LIVE-RUN PROOF for change 277 (user32!CharUpperBuffW and user32!CharLowerBuffW).
//
// Both exports are patched at once, because the pair is the change: they are the same loop with two
// tables and two ranges -- 'a'..'z' minus 0x20 going up, 'A'..'Z' plus 0x20 coming down -- and a
// proof that patched only one would be a proof about half a change. discovery/rtl_integer_char.c
// measured the lower form at 1.16 ns per character against the upper one's 0.78, so they are not
// even the same code underneath.
//
// The tables are built before the patch exists, and they have to be: tables.c builds them by asking
// CharUpperBuffW and CharLowerBuffW 65536 questions each. Under the patch they would be asking our
// code what our code should say.
//
// What is compared is the return value and every character of the buffer, including the characters
// Past the count. These exports take a count, not a terminator -- probes/mapping.c measured them
// mapping straight past an embedded NUL -- so an implementation that rounded the count up to a whole
// vector block would corrupt what follows, which is change 016's defect exactly and which only a
// whole-buffer comparison sees.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded, patching only its own copy-on-write copy.
//   (1) Validate first against the live exports before any patch exists.
//   (2) Patch only when idle: single-threaded, and neither export is used by the loader or the heap.
//   (3) REVERSIBLE: both prologues restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_charbuff_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

#pragma comment(lib, "user32.lib")

typedef DWORD (WINAPI *F_CB)(LPWSTR, DWORD);

extern DWORD wia_charupperbuffw(wchar_t*, DWORD);
extern DWORD wia_charlowerbuffw(wchar_t*, DWORD);
extern int   wia_cub_init(void);

static volatile LONG c_up, c_dn;
static DWORD WINAPI w_up(LPWSTR p, DWORD n)
{ _InterlockedIncrement(&c_up); return wia_charupperbuffw((wchar_t*)p, n); }
static DWORD WINAPI w_dn(LPWSTR p, DWORD n)
{ _InterlockedIncrement(&c_dn); return wia_charlowerbuffw((wchar_t*)p, n); }

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
#define BUFCH  320                 /* characters in the working buffer, count plus slack */
#define POISON 0x5A5A

typedef struct { DWORD ret; unsigned long long hash; } rec_t;
static rec_t expected[NCASE];

static wchar_t cur[BUFCH];
static DWORD cur_n;
static int cur_lower, cur_cls;

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

static void build_case(long i)
{
    unsigned k;
    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    cur_lower = (int)((i / 5) & 1);              /* co-prime with the class, so every class does both */
    cur_cls = (int)(i % 5);
    cur_n = (DWORD)(rnd() % 260);

    for (k = 0; k < BUFCH; ++k) cur[k] = (wchar_t)POISON;
    for (k = 0; k < cur_n; ++k) {
        switch (cur_cls) {
        case 0:  cur[k] = (wchar_t)('a' + (rnd() % 26)); break;          /* pure ASCII lower */
        case 1:  cur[k] = (wchar_t)('A' + (rnd() % 26)); break;          /* pure ASCII upper */
        case 2:  cur[k] = (wchar_t)(rnd() & 0xFFFF); break;              /* anything at all */
        case 3:  cur[k] = (wchar_t)(0x0100 + (rnd() % 0xFE00)); break;   /* all above 0x80 */
        default: cur[k] = (wchar_t)('a' + (rnd() % 26)); break;          /* ASCII with one high */
        }
    }
    if (cur_cls == 4 && cur_n) cur[rnd() % cur_n] = (wchar_t)(0x0080 + (rnd() % 0xFF00));
}

static void run_case(F_CB up, F_CB dn, rec_t* out)
{
    static wchar_t work[BUFCH];
    memcpy(work, cur, sizeof work);
    out->ret = cur_lower ? dn(work, cur_n) : up(work, cur_n);
    out->hash = fnv(work, sizeof work);          /* the WHOLE buffer, past the count included */
}

int main(void)
{
    HMODULE hu = GetModuleHandleW(L"user32.dll");
    F_CB live_up, live_dn;
    patch_t pu, pd;
    long i;
    long n_ascii = 0, n_high = 0, n_mixed = 0, n_zero = 0;

    if (!hu) hu = LoadLibraryW(L"user32.dll");
    live_up = (F_CB)GetProcAddress(hu, "CharUpperBuffW");
    live_dn = (F_CB)GetProcAddress(hu, "CharLowerBuffW");
    if (!live_up || !live_dn) { printf("resolve failed\n"); return 1; }

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: user32!CharUpperBuffW + CharLowerBuffW (change 277) ==\n");
    printf("  they resolve to %p and %p\n", (void*)live_up, (void*)live_dn);

    /* Before the patch exists: the tables build themselves by asking these very exports. */
    if (wia_cub_init()) { printf("  FAIL: the case tables failed to build\n"); return 1; }

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live_up, live_dn, &expected[i]);
        if (cur_cls <= 1) ++n_ascii;
        else if (cur_cls == 3) ++n_high;
        else if (cur_cls == 4) ++n_mixed;
        if (cur_n == 0) ++n_zero;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED exports;  %ld pure ASCII (the\n"
           "               in-register path), %ld entirely above 0x80 (the table path), %ld ASCII\n"
           "               with ONE high code unit (one block falls back, the rest do not), and\n"
           "               %ld with a count of zero\n",
           NCASE, n_ascii, n_high, n_mixed, n_zero);
    OK(n_ascii > 8000, "the corpus rarely took the in-register path");
    OK(n_high  > 4000, "the corpus rarely took the table path");
    OK(n_mixed > 4000, "the corpus rarely mixed the two, which is where the block boundary matters");
    OK(n_zero  > 50,   "the corpus rarely used a count of zero, which must touch nothing");

    {
        long differ = 0;
        rec_t got;
        if (!patch_on(&pu, (void*)live_up, (void*)w_up)) { printf("  FAIL: patch (upper)\n"); return 1; }
        if (!patch_on(&pd, (void*)live_dn, (void*)w_dn)) { printf("  FAIL: patch (lower)\n");
                                                           patch_off(&pu); return 1; }
        c_up = c_dn = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live_up, live_dn, &got);
            if (got.ret != expected[i].ret || got.hash != expected[i].hash) {
                if (++differ <= 8)
                    printf("  differ at %ld (%s, class %d, n %lu): returns %lu vs %lu%s\n",
                           i, cur_lower ? "lower" : "upper", cur_cls, (unsigned long)cur_n,
                           (unsigned long)expected[i].ret, (unsigned long)got.ret,
                           got.hash != expected[i].hash ? "  (the buffer differs)" : "");
            }
        }
        printf("  [patched]    %d cases, %ld differ (the return value and every character,\n"
               "               past the count included);  our-code calls = %ld + %ld\n",
               NCASE, differ, (long)c_up, (long)c_dn);
        OK(differ == 0, "an export answered differently under the patch");
        OK(c_up + c_dn == (LONG)NCASE, "the counters did not move once per call");
        OK(c_up > 10000 && c_dn > 10000, "one of the two directions was barely exercised");
        OK(patch_off(&pu), "the upper prologue was not restored byte-for-byte");
        OK(patch_off(&pd), "the lower prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG bu = c_up, bd = c_dn;
        rec_t got;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live_up, live_dn, &got);
            if (got.ret != expected[i].ret || got.hash != expected[i].hash) ++post;
        }
        printf("  [post]       %d cases through the RESTORED exports, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n",
               NCASE, post, (long)((c_up - bu) + (c_dn - bd)));
        OK(post == 0, "the restored exports no longer answer as they did");
        OK(c_up == bu && c_dn == bd, "our code still ran after the restore");
    }

    if (failures)
        printf("\nLIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    else
        printf("\nLIVE SUBSTITUTION: PASS (both exports patched together, the return value and every\n"
               "character of the buffer identical over %d cases with the characters PAST the count\n"
               "compared too, both the in-register and the table path driven, and both prologues\n"
               "restored byte-exact)\n", NCASE);
    return failures ? 1 : 0;
}
