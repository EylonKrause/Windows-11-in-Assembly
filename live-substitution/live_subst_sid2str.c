// live-substitution/live_subst_sid2str.c
// LIVE-RUN PROOF for change 270 (advapi32!ConvertSidToStringSidW).
//
// THIS EXPORT ALLOCATES, AND THAT IS THE PROPERTY NO OTHER GATE CAN CHECK. Every success hands the
// caller a LocalAlloc block that the CALLER frees through the process's ordinary, UNPATCHED
// LocalFree. An implementation that returned a static buffer, a HeapAlloc block, or a LocalAlloc
// block with the wrong flags would satisfy a byte-comparison gate and corrupt the caller's heap
// here. So every block is freed, and its LocalSize and LocalFlags are compared as well as its
// contents -- a block that is right but too big is still wrong.
//
// FIVE THINGS ARE COMPARED PER CASE: the BOOL, GetLastError (which on SUCCESS becomes ZERO whatever
// it was before), WHAT HAPPENED TO THE OUTPUT POINTER (a poison value distinguishes "left alone"
// from "cleared" from "written"), LocalSize/LocalFlags, and a hash of every byte of the block.
//
// THE SUB-AUTHORITY COUNT IS DRAWN OVER ITS WHOLE BYTE RANGE, 0..255, not 0..15. That is change
// 067's lesson learned the expensive way: its corpus drew the count as (seed>>8)%16 and therefore
// never expressed a count above 15, which is a refusal the implementation did not have -- and
// ConvertStringSidToSidW builds a 254-sub-authority SID in one call.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. Change 252's harness carried PRNG
// state across its three passes and reported 14285 differences with its patch counter at ZERO --
// the shipped export disagreeing with itself.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of the module -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and this export is used by neither the loader nor
//       the heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the whole corpus
//       is run again through the restored export.
//
// Build: build_sid2str_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef BOOL (WINAPI *F_S2S)(PSID, LPWSTR*);

extern BOOL wia_sid2str(const void*, wchar_t**);

static volatile LONG c_hit;
static BOOL WINAPI w_s2s(PSID s, LPWSTR* p)
{ _InterlockedIncrement(&c_hit); return wia_sid2str((const void*)s, (wchar_t**)p); }

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
#define POISON ((LPWSTR)(UINT_PTR)0xDEADBEEFDEADBEEFull)

static unsigned char sid[8 + 4 * 256];

typedef struct {
    unsigned char ok, ptr;
    DWORD  err;
    SIZE_T size;
    unsigned flags;
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

static const unsigned EDGE[] = {
    0u, 1u, 9u, 10u, 11u, 99u, 100u, 101u, 999u, 1000u, 9999u, 10000u, 99999u, 100000u,
    999999u, 1000000u, 9999999u, 10000000u, 99999999u, 100000000u, 999999999u, 1000000000u,
    2147483647u, 2147483648u, 4294967294u, 4294967295u
};
#define NEDGE (int)(sizeof EDGE / sizeof EDGE[0])

static int cur_cnt, cur_rev;

static void build_case(long i)
{
    int k, cls;
    unsigned long long auth;

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    cls = (int)(i % 5);
    cur_cnt = ((i % 9) == 4) ? (int)(16 + rnd() % 240) : (int)(rnd() % 16);
    cur_rev = ((i % 11) == 7) ? (int)(rnd() % 256) : 1;

    switch (cls) {
    case 0:  auth = rnd() % 32; break;
    case 1:  auth = rnd(); break;
    case 2:  auth = 0xFFFFFFFFull - (rnd() % 4); break;
    case 3:  auth = 0x100000000ull + (rnd() % 4); break;
    default: auth = ((unsigned long long)rnd() << 16) & 0xFFFFFFFFFFFFull; break;
    }

    sid[0] = (unsigned char)cur_rev;
    sid[1] = (unsigned char)cur_cnt;
    for (k = 0; k < 6; ++k) sid[2 + k] = (unsigned char)(auth >> (8 * (5 - k)));
    for (k = 0; k < 16; ++k) {
        unsigned v = (rnd() & 1) ? EDGE[rnd() % NEDGE] : rnd();
        sid[8 + 4 * k + 0] = (unsigned char)v;
        sid[8 + 4 * k + 1] = (unsigned char)(v >> 8);
        sid[8 + 4 * k + 2] = (unsigned char)(v >> 16);
        sid[8 + 4 * k + 3] = (unsigned char)(v >> 24);
    }
}

static void run_case(F_S2S f, rec_t* out)
{
    LPWSTR p = POISON;
    BOOL r;
    SetLastError(0xD15EA5E);
    r = f((PSID)sid, &p);
    out->err = GetLastError();
    out->ok = (unsigned char)(r ? 1 : 0);
    out->size = 0; out->flags = 0; out->hash = 0;
    if (p == POISON) { out->ptr = 0; return; }
    if (!p)          { out->ptr = 1; return; }
    out->ptr = 2;
    out->size = LocalSize(p);
    out->flags = (unsigned)LocalFlags(p);
    if (out->size != (SIZE_T)-1 && out->size <= 4096) out->hash = fnv(p, out->size);
    /* THROUGH THE UNPATCHED LocalFree. If our implementation handed back anything LocalFree does
       not own, this is where the process dies. */
    LocalFree(p);
}

int main(void)
{
    HMODULE ha = GetModuleHandleW(L"advapi32.dll");
    F_S2S live;
    patch_t pt;
    long i;
    long n_ok = 0, n_bad = 0, n_long = 0, n_hex = 0;

    if (!ha) ha = LoadLibraryW(L"advapi32.dll");
    live = (F_S2S)GetProcAddress(ha, "ConvertSidToStringSidW");
    if (!live) { printf("resolve failed\n"); return 1; }

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: advapi32!ConvertSidToStringSidW (change 270) ==\n");
    {
        HMODULE owner = 0;
        wchar_t path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)live, &owner))
            GetModuleFileNameW(owner, path, MAX_PATH);
        printf("  the export resolves to %p, which is in %ls\n", (void*)live, path);
    }

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live, &expected[i]);
        if (expected[i].ok) {
            ++n_ok;
            if (expected[i].size > 200) ++n_long;
            if ((unsigned char)sid[2] || (unsigned char)sid[3]) ++n_hex;
        } else if (expected[i].err == ERROR_INVALID_SID) ++n_bad;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  TRUE %ld (of which %ld gave a\n"
           "               block over 200 bytes, which is the 32-byte copy path, and %ld took the\n"
           "               HEXADECIMAL identifier authority), ERROR_INVALID_SID %ld\n",
           NCASE, n_ok, n_long, n_hex, n_bad);
    OK(n_ok   > 10000, "the corpus rarely succeeded");
    OK(n_bad  > 3000,  "the corpus rarely produced ERROR_INVALID_SID -- which is where a count above\n"
                       "        15 and a revision other than 1 both land");
    OK(n_long > 1000,  "the corpus rarely produced a block long enough to take the 32-byte copy");
    OK(n_hex  > 1000,  "the corpus rarely reached the hexadecimal identifier authority, which is a\n"
                       "        different converter and is never seen in an ordinary SID");

    {
        long differ = 0;
        rec_t got;
        if (!patch_on(&pt, (void*)live, (void*)w_s2s)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live, &got);
            if (got.ok != expected[i].ok || got.err != expected[i].err ||
                got.ptr != expected[i].ptr || got.size != expected[i].size ||
                got.flags != expected[i].flags || got.hash != expected[i].hash) {
                if (++differ <= 8)
                    printf("  differ at %ld (rev %d, count %d): live %d/err=%lu/ptr=%d/size=%Iu"
                           "   ours %d/err=%lu/ptr=%d/size=%Iu%s\n",
                           i, cur_rev, cur_cnt,
                           expected[i].ok, (unsigned long)expected[i].err, expected[i].ptr,
                           expected[i].size,
                           got.ok, (unsigned long)got.err, got.ptr, got.size,
                           (got.hash != expected[i].hash && got.ptr == 2 && expected[i].ptr == 2)
                               ? "  (the bytes differ)" : "");
            }
        }
        printf("  [patched]    %d cases, %ld differ (the BOOL, the last error, the output pointer,\n"
               "               LocalSize, LocalFlags and every byte of the block);  our-code calls = %ld\n",
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
                got.ptr != expected[i].ptr || got.size != expected[i].size ||
                got.flags != expected[i].flags || got.hash != expected[i].hash) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n",
               NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (the BOOL, GetLastError, the fate of the output\n"
                      "pointer, LocalSize, LocalFlags and every block byte identical over %d cases;\n"
                      "every block our code allocated was freed by the process's UNPATCHED LocalFree;\n"
                      "prologue restored byte-exact and the corpus re-run through it)\n",
           failures ? failures : NCASE);
    return failures ? 1 : 0;
}
