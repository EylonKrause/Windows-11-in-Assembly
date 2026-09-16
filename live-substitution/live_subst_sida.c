// live-substitution/live_subst_sida.c
// LIVE-RUN PROOF for change 272 (advapi32!ConvertStringSidToSidA).
//
// WHAT IS COMPARED IS FOUR THINGS: the BOOL, GetLastError(), what happened to the output pointer,
// and GetLengthSid plus a hash of every byte of the SID. Every allocated SID is freed through the
// process's UNPATCHED LocalFree.
//
// THE LAST ERROR IS COMPARED ON EVERY CALL, FROM A NON-ZERO SENTINEL. Change 269's first gate did
// neither -- it set the last error to zero before each call and then compared it only on FAILING
// ones -- and the two omissions together hid a real defect for a whole change: all four exports of
// the SID text family ZERO the last error on success, and 269's implementation did not.
// changes/272-.../probes/lasterror.c is the measurement; this harness is built so it could not
// happen here.
//
// TWO WIDENING PATHS ARE DRIVEN, because they are different code:
//   * an input with no byte at or above 0x80 is widened by a VPMOVZXBW zero extension, with no code
//     page consulted -- licensed by probes/asciilen.c, which measured every byte 0x00..0x7F against
//     all 22 code pages Windows can use as an ACP and found zero counterexamples;
//   * anything else calls MultiByteToWideChar.
// A corpus of plausible SID strings is entirely ASCII, so the fallback would never run. A quarter of
// the cases here are therefore built from high bytes on purpose, and the harness FAILS if that class
// comes back empty.
//
// AND THE ALIGNMENT IS SWEPT. The scan loads its first block ALIGNED DOWN and shifts out the bits
// before the string, so it is wrong at exactly the offsets nobody picks; every case runs at an
// offset taken from its index.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG
// state across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded, patching only its own copy-on-write copy.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and this export is used by neither loader nor heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_sida_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef BOOL (WINAPI *F_S2S)(LPCSTR, PSID*);

extern BOOL wia_str2sida(const char*, PSID*);
extern int  wia_sid_alias_init(void);
extern int  wia_sid_classify_init(void);

static volatile LONG c_hit;
static BOOL WINAPI w_s2s(LPCSTR s, PSID* p)
{ _InterlockedIncrement(&c_hit); return wia_str2sida((const char*)s, p); }

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
#define POISON ((PSID)(UINT_PTR)0xDEADBEEFDEADBEEFull)
#define SENTINEL 0x0D15EA5Eul

static char buf[SMAX + 128];
static char* cur;
static int   cur_cls, cur_high, cur_len, cur_off;

typedef struct {
    unsigned char ok, ptr;
    DWORD err, len;
    unsigned long long hash;
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

static void build_case(long i)
{
    int n = 0, k;
    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    cur_off = (int)(i % 64);                 /* the scan loads its first block aligned DOWN */
    cur = buf + cur_off;
    cur_cls = (int)(i % 6);

    switch (cur_cls) {
    case 0:                                  /* a well-formed SID */
    case 1:
        n += wsprintfA(cur + n, "S-%u-%u", 1 + rnd() % 3, rnd() % 22);
        { int ns = 1 + (int)(rnd() % 9);
          for (k = 0; k < ns; ++k) n += wsprintfA(cur + n, "-%u", rnd()); }
        break;
    case 2:                                  /* two characters: the alias table and its misses */
        cur[n++] = (char)(0x20 + rnd() % 95);
        cur[n++] = (char)(0x20 + rnd() % 95);
        break;
    case 3:                                  /* a valid SID with ONE byte corrupted, anywhere */
        n += wsprintfA(cur + n, "S-1-5");
        { int ns = 1 + (int)(rnd() % 5);
          for (k = 0; k < ns; ++k) n += wsprintfA(cur + n, "-%u", rnd() % 1000000); }
        cur[rnd() % (unsigned)n] = (char)(1 + rnd() % 255);
        break;
    case 4:                                  /* ENTIRELY HIGH BYTES: the code-page fallback */
        { int len = 1 + (int)(rnd() % 40);
          for (k = 0; k < len; ++k) cur[k] = (char)(0x80 + rnd() % 128);
          n = len; }
        break;
    default:                                 /* long: past where the temporary stops being the frame */
        { int target = 900 + (int)(rnd() % 300);
          n += wsprintfA(cur + n, "S-1-5");
          while (n < target) n += wsprintfA(cur + n, "-%09u", rnd() % 1000000000u);
          if ((rnd() & 3) == 0) cur[n / 2] = (char)(0x80 + rnd() % 128);
        }
        break;
    }
    cur[n] = 0;
    cur_len = n;
    cur_high = 0;
    for (k = 0; k < n; ++k) if ((unsigned char)cur[k] >= 0x80) { cur_high = 1; break; }
}

static void run_case(F_S2S f, rec_t* out)
{
    PSID p = POISON;
    BOOL r;
    SetLastError(SENTINEL);
    r = f(cur, &p);
    out->err = GetLastError();
    out->ok = (unsigned char)(r ? 1 : 0);
    out->len = 0; out->hash = 0;
    if (p == POISON) { out->ptr = 0; return; }
    if (!p)          { out->ptr = 1; return; }
    out->ptr = 2;
    if (IsValidSid(p)) { out->len = GetLengthSid(p); out->hash = fnv(p, out->len); }
    else out->len = 0xFFFFFFFFul;
    LocalFree(p);
}

int main(void)
{
    HMODULE ha = GetModuleHandleW(L"advapi32.dll");
    F_S2S live;
    patch_t pt;
    long i;
    long n_ok = 0, n_bad = 0, n_high = 0, n_long = 0, n_cleared = 0, n_zeroed = 0;

    if (!ha) ha = LoadLibraryW(L"advapi32.dll");
    live = (F_S2S)GetProcAddress(ha, "ConvertStringSidToSidA");
    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: advapi32!ConvertStringSidToSidA (change 272) ==\n");
    {
        HMODULE owner = 0;
        wchar_t path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)live, &owner))
            GetModuleFileNameW(owner, path, MAX_PATH);
        printf("  the export resolves to %p, which is in %ls\n", (void*)live, path);
    }

    /* BEFORE THE PATCH EXISTS: both tables build themselves by asking the WIDE export. */
    if (wia_sid_classify_init()) { printf("  FAIL: the character classes failed to build\n"); return 1; }
    if (wia_sid_alias_init())    { printf("  FAIL: the alias table failed to build\n"); return 1; }

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live, &expected[i]);
        if (expected[i].ok) { ++n_ok; if (expected[i].err == 0) ++n_zeroed; }
        else if (expected[i].err == ERROR_INVALID_SID) ++n_bad;
        if (!expected[i].ok && expected[i].ptr == 1) ++n_cleared;
        if (cur_high) ++n_high;
        if (cur_len > 1022) ++n_long;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  TRUE %ld (of which %ld came\n"
           "               back with the last error ZEROED from a non-zero sentinel),\n"
           "               ERROR_INVALID_SID %ld;  %ld inputs had a byte at or above 0x80 (the\n"
           "               code-page fallback) and %ld were longer than the stack temporary\n",
           NCASE, n_ok, n_zeroed, n_bad, n_high, n_long);
    OK(n_ok      > 4000, "the corpus rarely succeeded");
    OK(n_bad     > 8000, "the corpus rarely produced ERROR_INVALID_SID");
    OK(n_high    > 4000, "the corpus rarely contained a byte at or above 0x80 -- the ONLY path that\n"
                         "        consults the code page, and one a corpus of plausible SID strings\n"
                         "        never reaches");
    OK(n_long    > 2000, "the corpus rarely exceeded the stack temporary, so the allocated one and\n"
                         "        its free were barely exercised");
    OK(n_zeroed == n_ok, "a successful call did NOT always zero the last error -- which is the\n"
                         "        defect change 269 carried, hidden by a gate that used a zero\n"
                         "        sentinel and compared the last error only on failing calls");

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
                    printf("  differ at %ld (class %d, high %d, len %d, off %d): live %d/err=%lu/ptr=%d"
                           "   ours %d/err=%lu/ptr=%d%s\n",
                           i, cur_cls, cur_high, cur_len, cur_off,
                           expected[i].ok, (unsigned long)expected[i].err, expected[i].ptr,
                           got.ok, (unsigned long)got.err, got.ptr,
                           (got.hash != expected[i].hash && got.ptr == 2 && expected[i].ptr == 2)
                               ? "  (the SID bytes differ)" : "");
            }
        }
        printf("  [patched]    %d cases, %ld differ (the BOOL, the last error, the output pointer\n"
               "               and every SID byte);  our-code calls = %ld\n",
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
                    : "\nLIVE SUBSTITUTION: PASS (the BOOL, GetLastError from a non-zero sentinel on\n"
                      "every call, the fate of the output pointer and every SID byte identical over\n"
                      "%d cases, both widening paths and both temporaries driven; every SID our code\n"
                      "allocated was freed by the process's UNPATCHED LocalFree; prologue restored\n"
                      "byte-exact and the corpus re-run through it)\n",
           failures ? failures : NCASE);
    return failures ? 1 : 0;
}
