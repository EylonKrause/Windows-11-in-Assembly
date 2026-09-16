// live-substitution/live_subst_sid.c
// LIVE-RUN PROOF for change 269 (advapi32!ConvertStringSidToSidW).
//
// THIS EXPORT ALLOCATES, AND THAT IS THE PROPERTY NO OTHER GATE CAN CHECK. Every success hands the
// caller a LocalAlloc block that the CALLER frees, through the process's ordinary, UNPATCHED
// LocalFree. An implementation that returned a static buffer, a HeapAlloc block, or a LocalAlloc
// block with the wrong flags would satisfy correctness.c -- which compares bytes -- and would
// corrupt the caller's heap here. So every allocated SID in this harness is freed, all 38000-odd of
// them, and a bad block shows up as a crash or a heap check rather than as a diff.
//
// WHAT IS COMPARED IS FOUR THINGS, NOT ONE:
//   - the BOOL,
//   - GetLastError(), which is the whole substance of three of this export's five exits
//     (ERROR_INVALID_SID, ERROR_INVALID_PARAMETER, ERROR_ARITHMETIC_OVERFLOW),
//   - WHAT HAPPENED TO THE OUTPUT POINTER: left alone, cleared to NULL, or written. A poison value
//     is stored before every call, so "left alone" is observable rather than assumed. This matters
//     because the three SDDL terminators `)`, `,` and `;` are the only characters in the whole
//     16-bit space that make a FAILING call write the pointer, and a harness that only looked at
//     the BOOL would call that identical,
//   - and, when a SID came back, GetLengthSid plus a hash of every byte of it.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass, never carried in an array of
// pointers and never advanced by a PRNG threaded through the three passes. Change 252's harness
// carried PRNG state across its passes and reported 14285 differences with its patch counter at
// ZERO -- the shipped export disagreeing with itself.
//
// THE TWO OS-DERIVED TABLES ARE BUILT BEFORE THE PATCH EXISTS, and they have to be: aliases.c and
// classify.c both build themselves by ASKING ConvertStringSidToSidW several thousand questions. If
// they ran while the patch was in place they would be asking our code what our code should say.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of the module -- never a live system process, never the file on disk.
//       Note that GetProcAddress resolves advapi32's forwarder, so the bytes actually patched are
//       sechost's; the harness prints which module it landed in.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and this export is used by neither the loader nor
//       the heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the whole corpus
//       is run again through the restored export.
//
// Build: build_sid_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef BOOL (WINAPI *F_S2S)(LPCWSTR, PSID*);

extern BOOL wia_str2sid(const wchar_t*, PSID*);
extern int  wia_sid_alias_init(void);
extern int  wia_sid_classify_init(void);
extern int  wia_sid_alias_count;                 /* how many the OS actually answered to */

static volatile LONG c_hit;
static BOOL WINAPI w_s2s(LPCWSTR s, PSID* p)
{ _InterlockedIncrement(&c_hit); return wia_str2sid((const wchar_t*)s, p); }

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
#define SMAX   4096

/* A value no allocation can return and no implementation can produce by accident. "Left alone" is
   the contract for every failure but three, and it cannot be measured without a poison. */
#define POISON ((PSID)(UINT_PTR)0xDEADBEEFDEADBEEFull)

static wchar_t cur[SMAX];
static int     cur_cls;

typedef struct {
    unsigned char ok;          /* the BOOL */
    unsigned char ptr;         /* 0 = left alone, 1 = NULL, 2 = a SID */
    DWORD         err;
    DWORD         len;         /* GetLengthSid, when there is a SID */
    unsigned long long hash;   /* of every byte of it */
} rec_t;

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

/* The Unicode decimal digits classify.c found: Arabic-Indic, Devanagari, Thai, fullwidth. A corpus
   of ASCII digits never reaches the lenient table at all. */
static const wchar_t DIGBASE[] = { 0x0030, 0x0660, 0x06F0, 0x0966, 0x0E50, 0xFF10 };

static void app(int* n, const wchar_t* s) { while (*s) cur[(*n)++] = *s++; }
static void appnum(int* n, unsigned long long v, int base, int digbase)
{
    wchar_t t[32];
    int k = 0;
    if (!v) t[k++] = (wchar_t)(DIGBASE[digbase] + 0);
    while (v) {
        unsigned d = (unsigned)(v % (unsigned)base);
        t[k++] = (d < 10) ? (wchar_t)(DIGBASE[digbase] + d) : (wchar_t)(L'a' + d - 10);
        v /= (unsigned)base;
    }
    while (k) cur[(*n)++] = t[--k];
}

/* THE CASE IS A PURE FUNCTION OF ITS INDEX. Eight classes, and the dials are deliberately given
   co-prime strides so that no class is pinned to one shape of the others -- change 268's harness
   took its direction from the low bit and its content from index-modulo-six, and the surrogate
   class landed only on the direction that has no surrogates in its input. */
static void build_case(long i)
{
    int n = 0, k, nsub;
    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    cur_cls = (int)(i % 8);

    switch (cur_cls) {
    case 0:                                       /* a well-formed decimal SID */
        app(&n, L"S-");
        appnum(&n, 1 + (rnd() % 3), 10, 0);
        cur[n++] = L'-';
        appnum(&n, rnd() % 22, 10, 0);
        nsub = 1 + (int)(rnd() % 8);
        for (k = 0; k < nsub; ++k) {
            cur[n++] = L'-';
            appnum(&n, ((unsigned long long)rnd() << 1) % 0x100000000ull, 10, 0);
        }
        break;

    case 1: {
        /* TWO CHARACTERS: THE ALIAS TABLE, AND IT IS SWEPT RATHER THAN SAMPLED. The first draft
           drew both characters at random from the 95 printable ASCII codes; 264 of those 9025
           pairs are aliases, so 5000 random draws reached 104 of them and the run said so. A
           corpus that touches 40 % of the one table this change builds from the OS is not a
           proof about that table. The letter pairs -- where aliases.c measured every alias on
           this machine to live -- are therefore ENUMERATED, all 2704 of them, and the remaining
           slots keep drawing from the whole printable space so that "not an alias" is still
           proved for the rest of it. */
        unsigned t = (unsigned)(i / 8);
        unsigned a, b;
        if (t < 52u * 52u) {
            unsigned x = t / 52, y = t % 52;
            a = (x < 26) ? (unsigned)(L'A' + x) : (unsigned)(L'a' + x - 26);
            b = (y < 26) ? (unsigned)(L'A' + y) : (unsigned)(L'a' + y - 26);
            cur[n++] = (wchar_t)a;
            cur[n++] = (wchar_t)b;
        } else {
            a = 0x20 + rnd() % 95; b = 0x20 + rnd() % 95;
            cur[n++] = (wchar_t)a;
            cur[n++] = (wchar_t)b;
            /* and a quarter of those get a third character, which is never an alias */
            if ((rnd() & 3) == 0) cur[n++] = (wchar_t)(0x20 + (rnd() % 95));
        }
        break;
    }

    case 2:                                       /* the hexadecimal carry: 0x on the revision */
        app(&n, (rnd() & 1) ? L"S-0x" : L"S-0X");
        appnum(&n, 1 + (rnd() % 3), 16, 0);
        cur[n++] = L'-';
        appnum(&n, rnd() % 0x40, 16, 0);
        nsub = 1 + (int)(rnd() % 6);
        for (k = 0; k < nsub; ++k) {
            cur[n++] = L'-';
            /* no prefix: past the carry the prefix is optional, which is the whole point */
            appnum(&n, ((unsigned long long)rnd() << 1) % 0x100000000ull, 16, 0);
        }
        break;

    case 3: {                                     /* the Unicode decimal digits */
        int db = 1 + (int)(rnd() % 5);
        app(&n, L"S-");
        appnum(&n, 1 + (rnd() % 3), 10, (rnd() & 1) ? db : 0);
        cur[n++] = L'-';
        appnum(&n, rnd() % 22, 10, db);
        nsub = 1 + (int)(rnd() % 4);
        for (k = 0; k < nsub; ++k) {
            cur[n++] = L'-';
            /* the SUB-authority parser has the STRICT digit set: only ASCII and fullwidth. Most of
               these are therefore refusals, and that asymmetry is exactly what is being proved. */
            appnum(&n, rnd() % 100000, 10, (int)(rnd() % 6));
        }
        break;
    }

    case 4:                                       /* whitespace and the single '+' the lenient
                                                     parser takes, in the two fields that take it
                                                     and in the one that does not */
        app(&n, L"S-");
        if (rnd() & 1) cur[n++] = (wchar_t)L" \t\n\v\f\r"[rnd() % 6];
        if (rnd() & 1) cur[n++] = L'+';
        appnum(&n, 1 + (rnd() % 2), 10, 0);
        cur[n++] = L'-';
        if (rnd() & 1) cur[n++] = L' ';
        if (rnd() & 1) cur[n++] = L'+';
        appnum(&n, rnd() % 22, 10, 0);
        nsub = 1 + (int)(rnd() % 3);
        for (k = 0; k < nsub; ++k) {
            cur[n++] = L'-';
            if (rnd() & 1) cur[n++] = L' ';
            if (rnd() & 1) cur[n++] = L'+';
            appnum(&n, rnd() % 100000, 10, 0);
        }
        break;

    case 5: {                                     /* a valid SID with ONE character corrupted */
        int pos;
        app(&n, L"S-");
        appnum(&n, 1 + (rnd() % 2), 10, 0);
        cur[n++] = L'-';
        appnum(&n, rnd() % 22, 10, 0);
        nsub = 1 + (int)(rnd() % 5);
        for (k = 0; k < nsub; ++k) {
            cur[n++] = L'-';
            appnum(&n, rnd() % 1000000, 10, 0);
        }
        pos = (int)(rnd() % (unsigned)n);
        /* the whole 16-bit space, not just punctuation: probes/bounds.c found that whether a
           character is a digit, a space or neither is a THREE-way answer with 65535 entries */
        cur[pos] = (wchar_t)(1 + (rnd() % 0xFFFF));
        break;
    }

    case 6:                                       /* a COMPLETE SID, then one trailing character --
                                                     `)`, `,` and `;` clear the pointer, the other
                                                     65532 leave it alone */
        app(&n, L"S-1-5");
        nsub = 1 + (int)(rnd() % 4);
        for (k = 0; k < nsub; ++k) {
            cur[n++] = L'-';
            appnum(&n, rnd() % 100000, 10, 0);
        }
        switch (rnd() % 8) {
        case 0: cur[n++] = L')'; break;
        case 1: cur[n++] = L','; break;
        case 2: cur[n++] = L';'; break;
        case 3: break;                            /* the control: nothing trailing */
        default: cur[n++] = (wchar_t)(1 + (rnd() % 0xFFFF)); break;
        }
        break;

    default: {                                    /* the three limits: 254 sub-authorities, a
                                                     revision above 255, an authority above 48 bits */
        unsigned pick = rnd() % 3;
        if (pick == 0) {
            int cnt = 248 + (int)(rnd() % 12);     /* straddles the 254 that returns OVERFLOW */
            app(&n, L"S-1-5");
            for (k = 0; k < cnt; ++k) {
                cur[n++] = L'-';
                appnum(&n, (unsigned)(k % 9) + 1, 10, 0);
            }
        } else if (pick == 1) {
            app(&n, L"S-");
            appnum(&n, 250 + (rnd() % 12), 10, 0); /* straddles 255 */
            app(&n, L"-5-1");
        } else {
            app(&n, L"S-1-");
            appnum(&n, 281474976710650ull + (rnd() % 12), 10, 0);  /* straddles 2^48 */
            app(&n, L"-1");
        }
        break;
    }
    }
    cur[n] = 0;
}

static void run_case(F_S2S f, rec_t* out)
{
    PSID p = POISON;
    BOOL r;
    SetLastError(0);
    r = f(cur, &p);
    out->err = GetLastError();
    out->ok  = (unsigned char)(r ? 1 : 0);
    out->len = 0;
    out->hash = 0;
    if (p == POISON) { out->ptr = 0; return; }
    if (p == 0)      { out->ptr = 1; return; }
    out->ptr = 2;
    /* THE BLOCK IS FREED THROUGH THE UNPATCHED LocalFree. If our implementation handed back
       anything LocalFree does not own, this is where the process dies. */
    if (IsValidSid(p)) {
        out->len = GetLengthSid(p);
        out->hash = fnv(p, out->len);
    } else {
        out->len = 0xFFFFFFFFul;
    }
    LocalFree(p);
}

int main(void)
{
    HMODULE ha = GetModuleHandleW(L"advapi32.dll");
    F_S2S   live;
    patch_t pt;
    long i;
    long n_ok = 0, n_bad = 0, n_ovf = 0, n_prm = 0, n_alias = 0, n_clr = 0, n_alone = 0;
    static unsigned char seen_alias[95 * 95];
    long n_distinct = 0;

    if (!ha) ha = LoadLibraryW(L"advapi32.dll");
    live = (F_S2S)GetProcAddress(ha, "ConvertStringSidToSidW");
    if (!live) { printf("resolve failed\n"); return 1; }

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: advapi32!ConvertStringSidToSidW (change 269) ==\n");
    {
        /* advapi32 forwards this one. Say where the bytes being patched actually live, because
           "advapi32" in the title would otherwise be a guess. */
        HMODULE owner = 0;
        wchar_t path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)live, &owner))
            GetModuleFileNameW(owner, path, MAX_PATH);
        printf("  the export resolves to %p, which is in %ls\n", (void*)live, path);
    }

    /* BEFORE THE PATCH EXISTS, and it has to be: both tables build themselves by asking this very
       export several thousand questions. */
    if (wia_sid_classify_init()) { printf("  FAIL: the character classes failed to build\n"); return 1; }
    if (wia_sid_alias_init())    { printf("  FAIL: the alias table failed to build\n"); return 1; }

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live, &expected[i]);
        if (expected[i].ok && cur_cls == 1 && cur[0] && cur[1] && !cur[2]) {
            /* DISTINCT aliases reached, not alias calls made: 264 hits could be one alias asked
               264 times, and that is the shape the first draft of this corpus had. */
            unsigned a = (unsigned)cur[0] - 0x20, b = (unsigned)cur[1] - 0x20;
            if (a < 95 && b < 95 && !seen_alias[a * 95 + b]) { seen_alias[a * 95 + b] = 1; ++n_distinct; }
        }
        if (expected[i].ok) { ++n_ok; if (cur_cls == 1) ++n_alias; }
        else if (expected[i].err == ERROR_INVALID_SID)        ++n_bad;
        else if (expected[i].err == ERROR_ARITHMETIC_OVERFLOW) ++n_ovf;
        else if (expected[i].err == ERROR_INVALID_PARAMETER)   ++n_prm;
        if (!expected[i].ok && expected[i].ptr == 1) ++n_clr;
        if (!expected[i].ok && expected[i].ptr == 0) ++n_alone;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  TRUE %ld (of which %ld\n"
           "               through the alias table, reaching %ld DISTINCT aliases of the %d this\n"
           "               machine has), ERROR_INVALID_SID %ld, ERROR_ARITHMETIC_OVERFLOW %ld,\n"
           "               ERROR_INVALID_PARAMETER %ld;  failures that CLEARED the pointer %ld,\n"
           "               failures that LEFT IT ALONE %ld\n",
           NCASE, n_ok, n_alias, n_distinct, wia_sid_alias_count, n_bad, n_ovf, n_prm,
           n_clr, n_alone);
    OK(n_ok    > 5000, "the corpus rarely succeeded");
    OK(n_alias > 200,  "the corpus barely reached the alias table");
    OK(n_distinct >= wia_sid_alias_count,
                       "the corpus did not reach EVERY alias this machine has -- 264 alias calls\n"
                       "        could be one alias asked 264 times, and the first draft of this\n"
                       "        corpus reached 104 of them by sampling instead of sweeping");
    OK(n_bad   > 5000, "the corpus rarely produced ERROR_INVALID_SID");
    OK(n_ovf   > 100,  "the corpus never reached the 254-sub-authority OVERFLOW exit");
    OK(n_clr   > 100,  "the corpus never hit an SDDL terminator -- the one failure shape that\n"
                       "        WRITES the output pointer, and the one a BOOL-only harness misses");
    OK(n_alone > 5000, "the corpus rarely took an ordinary failure, which leaves the pointer alone");

    {
        long differ = 0;
        rec_t got;
        if (!patch_on(&pt, (void*)live, (void*)w_s2s)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.ok != expected[i].ok || got.err != expected[i].err ||
                got.ptr != expected[i].ptr || got.len != expected[i].len ||
                got.hash != expected[i].hash) {
                if (++differ <= 8)
                    printf("  differ at %ld (class %d, \"%.60ls\"): live %d/err=%lu/ptr=%d/len=%lu"
                           "   ours %d/err=%lu/ptr=%d/len=%lu%s\n",
                           i, cur_cls, cur,
                           expected[i].ok, (unsigned long)expected[i].err, expected[i].ptr,
                           (unsigned long)expected[i].len,
                           got.ok, (unsigned long)got.err, got.ptr, (unsigned long)got.len,
                           (got.hash != expected[i].hash && got.ptr == 2 && expected[i].ptr == 2)
                               ? "  (the SID bytes differ)" : "");
            }
        }
        printf("  [patched]    %d cases, %ld differ (the BOOL, the last error, what happened to the\n"
               "               output pointer, and every byte of the SID);  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
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
            if (got.ok != expected[i].ok || got.err != expected[i].err ||
                got.ptr != expected[i].ptr || got.len != expected[i].len ||
                got.hash != expected[i].hash) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n",
               NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (the BOOL, GetLastError, the fate of the output\n"
                      "pointer and every SID byte identical over %d cases; every SID our code\n"
                      "allocated was freed by the process's UNPATCHED LocalFree; prologue restored\n"
                      "byte-exact and the corpus re-run through it)\n",
           failures ? failures : NCASE);
    return failures ? 1 : 0;
}
