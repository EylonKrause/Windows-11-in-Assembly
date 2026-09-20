// live-substitution/live_subst_findsub.c
// LIVE-RUN PROOF for change 252 (ntdll!RtlFindUnicodeSubstring).
//
// The export is hot-patched in a sacrificial child so that every subsequent call BY NAME runs our
// assembly, and the answers are required to be identical to the ones the shipped export gave for
// the same corpus BEFORE the patch existed.
//
// Who actually calls this, stated because it bounds what a live proof here can mean. a scan of
// System32 for importers of the name finds ntoskrnl.exe, afd.sys, dxgkrnl.sys, dam.sys, CAD.sys and
// VerifierExt.sys -- all KERNEL mode, none of them reachable from a user-mode patch -- plus exactly
// two user-mode DLLs: wldp.dll and ci.dll. Neither yields a second export the way change 249's
// UrlHashW or change 250's ExW did: wldp's two call sites are at RVA 0001AE8E and 0001AF10, inside
// an internal helper 0x15DE past the nearest preceding export, reachable only by driving a lockdown
// policy query down a particular path; and ci.dll imports the name but has ZERO direct call sites in
// its .text, so it reaches it (if at all) through a pointer. So this harness proves the export
// itself, and claims nothing more.
//
// What is compared: the offset of the hit, or -1 -- not the raw pointer -- for every case in both
// modes, so an answer cannot be right by accident of where the buffer happens to sit.
//
// The table trap, which this file calls out deliberately. wia_casemate_init() is called before
// anything else. Leaving it out does not fail loudly: the case-partner table would be all zeros,
// every partner would be U+0000, and the search would still return a perfectly well-formed answer
// -- just one that never matches across case. Change 065 cost an entire session to exactly this
// shape of silent failure (its two-digit table went unbuilt, and every IP address came out
// truncated with a correct status and a correct length), so the call is made first and said out
// loud here.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and this routine is used by neither the loader nor
//       the heap.
//   (3) REVERSIBLE: original bytes restored, and the restore is VERIFIED byte-for-byte, then the
//       whole corpus is run again through the restored export.
//
// Build: build_findsub_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef PWSTR (NTAPI *FFIND)(USTR*, USTR*, BOOLEAN);

extern PWSTR wia_findunicodesubstring(void*, void*, BOOLEAN);
extern int   wia_casemate_init(void);

static volatile LONG c_find;
static PWSTR NTAPI w_find(USTR* f, USTR* s, BOOLEAN ci)
{
    _InterlockedIncrement(&c_find);
    return wia_findunicodesubstring(f, s, ci);
}

/* ---- the hot patch: a 14-byte absolute indirect jump, saved and restored exactly ---- */
typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;

static void raw_copy(volatile unsigned char* dst, const volatile unsigned char* src, int n)
{
    int i;
    for (i = 0; i < n; ++i) dst[i] = src[i];
}
static int patch_on(patch_t* p, void* target, void* repl)
{
    DWORD old;
    unsigned char stub[14];
    p->target = target; p->on = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    stub[0] = 0xFF; stub[1] = 0x25;
    *(uint32_t*)(stub + 2) = 0;
    *(uint64_t*)(stub + 6) = (uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p)
{
    DWORD old;
    int i;
    if (!p->on) return 1;
    VirtualProtect(p->target, 16, PAGE_EXECUTE_READWRITE, &old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for (i = 0; i < 16; ++i)
        if (((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

static int failures = 0;
#define OK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", (msg)); ++failures; } } while (0)

/* ---------------------------------------------------------------------------------------------
   The corpus. Every case is generated into a fixed pair of buffers, so the OFFSET is the answer.
   Shapes covered: the empty needle and the empty haystack; a needle longer than the haystack; the
   scalar-only path (fewer than sixteen start positions); the vector loop with a miss, with a hit
   inside a block and with a hit in the scalar tail; the degenerate needle that makes the far-anchor
   choice fall back to m-1; a two-letter alphabet; and non-ASCII text with case-differing hits,
   which is the only family that exercises the case-partner broadcasts against real pairs.
   --------------------------------------------------------------------------------------------- */
#define HMAX 1024
#define NMAX 64
static wchar_t hb[HMAX], nb[NMAX];
static int cur_hn, cur_nn, cur_ci;

static unsigned long long rs = 0xC2B2AE3D27D4EB4Full;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

#define NCASE 60000
static void build_case(long i)
{
    int family = (int)(i % 6);
    int n, m, k, pos;

    /* Reseed from the case index, so case i is the same string on every pass.
       This is not a detail. The corpus is generated three times -- once to record what the shipped
       export says, once through the patch, once after the restore -- and with a PRNG whose state
       carried across passes the three passes built three DIFFERENT corpora. The first run of this
       harness reported 14252 differences under the patch and, decisively, 14285 differences through
       the RESTORED export with our counter at zero: the shipped export disagreeing with itself,
       which is only possible if the question changed. The post-restore pass exists precisely to
       catch that, and it did. */
    rs = 0xC2B2AE3D27D4EB4Full ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    cur_ci = (int)((i / 6) & 1);
    switch (family) {
    case 0:                                        /* degenerate lengths */
        n = (int)(i % 40); m = (int)((i / 40) % 8);
        for (k = 0; k < n; ++k) hb[k] = (wchar_t)(L'a' + (k % 4));
        for (k = 0; k < m; ++k) nb[k] = (wchar_t)(L'a' + (k % 4));
        break;
    case 1:                                        /* scalar-only: fewer than 16 start positions */
        m = 1 + (int)(i % 12); n = m + (int)((i / 12) % 15);
        for (k = 0; k < n; ++k) hb[k] = (wchar_t)(L'a' + (rnd() % 5));
        for (k = 0; k < m; ++k) nb[k] = (wchar_t)(L'a' + (rnd() % 5));
        break;
    case 2:                                        /* the vector loop, ASCII, hit or miss */
        n = 40 + (int)(i % 900); m = 1 + (int)((i / 7) % 20);
        if (m > n) m = n;
        for (k = 0; k < n; ++k) hb[k] = (wchar_t)(L'a' + (rnd() % 26));
        for (k = 0; k < m; ++k) nb[k] = (wchar_t)(L'a' + (rnd() % 26));
        if (rnd() & 1) { pos = (int)(rnd() % (unsigned)(n - m + 1));
                         for (k = 0; k < m; ++k) hb[pos + k] = nb[k]; }
        break;
    case 3:                                        /* the degenerate needle: every character equal */
        n = 40 + (int)(i % 900); m = 2 + (int)((i / 5) % 10);
        if (m > n) m = n;
        for (k = 0; k < n; ++k) hb[k] = L'a';
        for (k = 0; k < m; ++k) nb[k] = (k == 0 || k == m - 1) ? L'a' : L'z';
        if ((i & 3) == 0) for (k = 0; k < m; ++k) nb[k] = L'a';
        break;
    case 4:                                        /* two letters: a dense false-anchor field */
        n = 40 + (int)(i % 900); m = 2 + (int)((i / 3) % 16);
        if (m > n) m = n;
        for (k = 0; k < n; ++k) hb[k] = (wchar_t)(L'a' + (rnd() % 2));
        for (k = 0; k < m; ++k) nb[k] = (wchar_t)(L'a' + (rnd() % 2));
        if (rnd() & 1) nb[m - 1] = L'c';
        break;
    default:                                       /* non-ASCII, with case-differing plants */
        n = 40 + (int)(i % 900); m = 1 + (int)((i / 11) % 16);
        if (m > n) m = n;
        for (k = 0; k < n; ++k) hb[k] = (wchar_t)(0x0410 + (rnd() % 64));   /* Cyrillic both cases */
        for (k = 0; k < m; ++k) nb[k] = (wchar_t)(0x0410 + (rnd() % 32));   /* upper */
        if ((i & 1) == 0) {                        /* plant the LOWERCASE form: only CI can find it */
            pos = (int)(rnd() % (unsigned)(n - m + 1));
            for (k = 0; k < m; ++k) hb[pos + k] = (wchar_t)(nb[k] + 32);
        }
        if ((i % 17) == 0) {                       /* the ASCII/non-ASCII crossover pair */
            hb[n / 2] = 0x017F; nb[0] = L'S'; if (m > 1) nb[1] = hb[n / 2 + 1];
        }
        break;
    }
    cur_hn = n; cur_nn = m;
}

static int run_once(FFIND f)
{
    USTR H, N;
    PWSTR r;
    H.Buffer = hb; H.Length = (USHORT)(cur_hn * 2); H.MaximumLength = H.Length;
    N.Buffer = nb; N.Length = (USHORT)(cur_nn * 2); N.MaximumLength = N.Length;
    r = f(&H, &N, (BOOLEAN)cur_ci);
    return r ? (int)(r - hb) : -1;
}

static int  expect[NCASE];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    FFIND live = (FFIND)GetProcAddress(h, "RtlFindUnicodeSubstring");
    patch_t p;
    long i;
    int maxclass;
    long differ = 0;
    LONG calls_expected;

    if (!live) { printf("RtlFindUnicodeSubstring not found\n"); return 1; }

    /* The table first -- see the header. An unbuilt table fails silently, not loudly. */
    maxclass = wia_casemate_init();
    printf("== LIVE SUBSTITUTION: ntdll!RtlFindUnicodeSubstring (change 252) ==\n");
    printf("  case-partner table built; largest case-equivalence class = %d (must be 2)\n", maxclass);
    OK(maxclass == 2, "the OS upcase table has a class larger than two -- the filter is not exact");
    if (maxclass != 2) return 1;

    printf("  export at %p\n", (void*)live);

    /* ---- (1) Validate first: record what the shipped export says, before any patch ---- */
    for (i = 0; i < NCASE; ++i) { build_case(i); expect[i] = run_once(live); }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export\n", NCASE);

    /* ---- (2) PATCH ---- */
    if (!patch_on(&p, (void*)live, (void*)w_find)) { printf("  FAIL: could not patch\n"); return 1; }
    printf("  [patched]    export redirected to our assembly\n");

    /* ---- (3) the same corpus, through the export by name ---- */
    c_find = 0;
    for (i = 0; i < NCASE; ++i) {
        int got;
        build_case(i);
        got = run_once(live);
        if (got != expect[i]) {
            if (differ < 10)
                printf("  DIFFER case %ld: shipped=%d ours=%d  (n=%d m=%d ci=%d)\n",
                       i, expect[i], got, cur_hn, cur_nn, cur_ci);
            ++differ;
        }
    }
    calls_expected = (LONG)NCASE;
    printf("  [patched]    %d cases, %ld differ;  our-code calls through the export = %ld\n",
           NCASE, differ, (long)c_find);
    OK(differ == 0, "an answer through the patched export differs from the shipped one");
    OK(c_find == calls_expected,
       "the counter did not move by one per call -- the patch was not actually taken");

    /* ---- (4) RESTORE, and verify the bytes are identical ---- */
    OK(patch_off(&p), "the restored prologue is NOT byte-identical to the original");
    printf("  [restored]   prologue verified byte-for-byte\n");

    /* ---- (5) and prove the restored export still answers as it did ---- */
    {
        long post = 0;
        LONG before = c_find;
        for (i = 0; i < NCASE; ++i) { build_case(i); if (run_once(live) != expect[i]) ++post; }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n", NCASE, post, (long)(c_find - before));
        OK(post == 0, "the restored export no longer answers as it did before the patch");
        OK(c_find == before, "our code still ran after the restore -- the patch did not come off");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (patched, proved by counter, restored byte-exact)\n",
           failures);
    return failures ? 1 : 0;
}
