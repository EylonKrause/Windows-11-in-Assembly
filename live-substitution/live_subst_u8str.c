// live-substitution/live_subst_u8str.c
// LIVE-RUN PROOF for change 268 (ntdll!RtlUnicodeStringToUTF8String and ntdll!RtlUTF8StringToUnicodeString).
//
// Both exports are patched at once, because the pair is the change: the whole point of 268 is that
// the two directions do FOUR things differently -- what a failing call leaves in the buffer,
// whether STATUS_SOME_NOT_MAPPED survives, which of two failure codes a shortfall gets, and how
// much room the terminator needs -- and a proof that patched only one of them would be a proof
// about half a change.
//
// What is compared is not just the status. Every case records the NTSTATUS, Length, MaximumLength
// AND a hash of the whole destination buffer, because these functions leave the destination
// partially written on a failing call in one direction and untouched in the other, and an
// implementation that tidied that up would pass any check that only read the status. That is not
// hypothetical here: the first build of change 268 against the current converters found 154
// mismatches that were nothing but a single 00 byte past the end of the string.
//
// The allocating path is the one that could corrupt a heap, so it is in the corpus and every block
// it returns is freed through the UNPATCHED RtlFreeUTF8String / RtlFreeUnicodeString. That is the
// property that matters and cannot be checked any other way: our implementation must allocate a
// block the shipped free routine will accept.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng
// state across its three passes and reported 14285 differences with its counter at ZERO -- the
// shipped export disagreeing with itself.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live exports before any patch exists.
//   (2) Patch only when idle: single-threaded, and neither export is used by the loader or heap.
//   (3) REVERSIBLE: original bytes restored, VERIFIED byte-for-byte, and the corpus run again.
//
// Build: build_u8str_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG (NTAPI *F_U2U8)(U8STR*, const USTR*, BOOLEAN);
typedef LONG (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);
typedef void (NTAPI *F_FreeU8)(U8STR*);
typedef void (NTAPI *F_FreeU)(USTR*);

extern LONG wia_unicodestringtoutf8string(U8STR*, const USTR*, BOOLEAN);
extern LONG wia_utf8stringtounicodestring(USTR*, const U8STR*, BOOLEAN);

static volatile LONG c_hit8, c_hitw;
static LONG NTAPI w_u2u8(U8STR* d, const USTR* s, BOOLEAN a)
{ _InterlockedIncrement(&c_hit8); return wia_unicodestringtoutf8string(d, s, a); }
static LONG NTAPI w_u82u(USTR* d, const U8STR* s, BOOLEAN a)
{ _InterlockedIncrement(&c_hitw); return wia_utf8stringtounicodestring(d, s, a); }

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
#define SRCMAX 600
#define DSTMAX 4096

static wchar_t wsrc[SRCMAX];
static char    u8src[SRCMAX];
static char    dbuf8[DSTMAX];
static wchar_t dbufw[DSTMAX];

typedef struct { LONG st; USHORT len, max; unsigned long long hash; } rec_t;
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

/* case shape, rebuilt from the index alone */
static int cur_dir, cur_alloc, cur_len;
static USHORT cur_cap;

static void build_case(long i)
{
    int k, cls;
    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    /* The three dials must not share a factor with each other. The first version took the
       direction from the low bit of the index and the content class from index modulo six, so the
       surrogate class -- index congruent to 3 -- was always an ODD index and therefore always the
       UTF-8 -> UTF-16 direction, which does not have surrogates in its input at all. The corpus
       produced STATUS_SOME_NOT_MAPPED exactly ZERO times, and the harness said so rather than
       letting the run pass with one of the four statuses never reached. */
    cls       = (int)(i % 6);
    cur_dir   = (int)((i / 6) & 1);
    cur_alloc = ((i % 9) == 0);
    cur_len   = (int)(rnd() % 120);
    for (k = 0; k < cur_len; ++k) {
        unsigned r = rnd();
        switch (cls) {
        case 0: wsrc[k] = (wchar_t)('a' + (r % 26));       u8src[k] = (char)('a' + (r % 26)); break;
        case 1: wsrc[k] = (wchar_t)(0x00A0 + (r % 0x60));  u8src[k] = (char)(0xC2 + (r % 0x1E)); break;
        case 2: wsrc[k] = (wchar_t)(0x0800 + (r % 0xD000));u8src[k] = (char)(0xE1 + (r % 0x0E)); break;
        case 3: wsrc[k] = (wchar_t)(0xD800 + (r % 0x800)); u8src[k] = (char)(0x80 + (r % 0x40)); break;
        case 4: wsrc[k] = (k & 1) ? (wchar_t)0x00E9 : (wchar_t)('a' + (r % 26));
                u8src[k] = (k % 3) ? (char)('a' + (r % 26)) : (char)0xC3;
                break;
        default: wsrc[k] = (wchar_t)r; u8src[k] = (char)r; break;
        }
    }
    /* capacities clustered on the boundaries the terminator rule lives at */
    switch ((int)((i / 12) % 6)) {
    case 0: cur_cap = 0; break;
    case 1: cur_cap = (USHORT)(cur_len ? 1 : 0); break;
    case 2: cur_cap = (USHORT)cur_len; break;
    case 3: cur_cap = (USHORT)(cur_len + 1); break;
    case 4: cur_cap = (USHORT)(cur_len * 2 + 2); break;
    default: cur_cap = (USHORT)2000; break;
    }
}

static void run_case(F_U2U8 f8, F_U82U fw, F_FreeU8 fr8, F_FreeU frw, rec_t* out)
{
    if (cur_dir == 0) {
        USTR in; U8STR d;
        in.Buffer = wsrc; in.Length = (USHORT)(cur_len * 2); in.MaximumLength = in.Length;
        if (cur_alloc) {
            memset(&d, 0xCD, sizeof d);
            out->st = f8(&d, &in, TRUE);
            out->len = d.Length; out->max = d.MaximumLength;
            out->hash = (out->st >= 0 && d.Buffer) ? fnv(d.Buffer, (size_t)d.Length + 1) : 0;
            if (out->st >= 0 && fr8) fr8(&d);
        } else {
            memset(dbuf8, '#', DSTMAX);
            d.Buffer = dbuf8; d.Length = 0xBEEF; d.MaximumLength = cur_cap;
            out->st = f8(&d, &in, FALSE);
            out->len = d.Length; out->max = d.MaximumLength;
            out->hash = fnv(dbuf8, DSTMAX);
        }
    } else {
        U8STR in; USTR d;
        in.Buffer = u8src; in.Length = (USHORT)cur_len; in.MaximumLength = in.Length;
        if (cur_alloc) {
            memset(&d, 0xCD, sizeof d);
            out->st = fw(&d, &in, TRUE);
            out->len = d.Length; out->max = d.MaximumLength;
            out->hash = (out->st >= 0 && d.Buffer) ? fnv(d.Buffer, (size_t)d.Length + 2) : 0;
            if (out->st >= 0 && frw) frw(&d);
        } else {
            memset(dbufw, 0x23, sizeof dbufw);
            d.Buffer = dbufw; d.Length = 0xBEEF; d.MaximumLength = cur_cap;
            out->st = fw(&d, &in, FALSE);
            out->len = d.Length; out->max = d.MaximumLength;
            out->hash = fnv(dbufw, sizeof dbufw);
        }
    }
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_U2U8   live8 = (F_U2U8)  GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    F_U82U   livew = (F_U82U)  GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    F_FreeU8 fr8   = (F_FreeU8)GetProcAddress(h, "RtlFreeUTF8String");
    F_FreeU  frw   = (F_FreeU) GetProcAddress(h, "RtlFreeUnicodeString");
    patch_t p8, pw;
    long i, n_ok = 0, n_small = 0, n_over = 0, n_nm = 0, n_alloc = 0;

    if (!live8 || !livew || !fr8 || !frw) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: ntdll!RtlUnicodeStringToUTF8String + RtlUTF8StringToUnicodeString"
           " (change 268) ==\n");

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        run_case(live8, livew, fr8, frw, &expected[i]);
        if (expected[i].st == 0) ++n_ok;
        else if ((ULONG)expected[i].st == 0xC0000023ul) ++n_small;
        else if ((ULONG)expected[i].st == 0x80000005ul) ++n_over;
        if (expected[i].st == 0x107) ++n_nm;
        if (cur_alloc) ++n_alloc;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED exports;  SUCCESS %ld,\n"
           "               BUFFER_TOO_SMALL %ld, BUFFER_OVERFLOW %ld, SOME_NOT_MAPPED %ld,\n"
           "               and %ld of them took the ALLOCATING path\n",
           NCASE, n_ok, n_small, n_over, n_nm, n_alloc);
    OK(n_ok    > 5000, "the corpus rarely succeeded");
    OK(n_small > 1000, "the corpus rarely produced STATUS_BUFFER_TOO_SMALL -- which only ONE of\n"
                       "        the two directions ever returns");
    OK(n_over  > 1000, "the corpus rarely produced STATUS_BUFFER_OVERFLOW");
    OK(n_nm    > 100,  "the corpus rarely produced STATUS_SOME_NOT_MAPPED -- which only ONE of the\n"
                       "        two directions passes through");
    OK(n_alloc > 2000, "the corpus rarely took the allocating path");

    {
        long differ = 0;
        rec_t got;
        if (!patch_on(&p8, (void*)live8, (void*)w_u2u8)) { printf("  FAIL: patch (UTF-8)\n"); return 1; }
        if (!patch_on(&pw, (void*)livew, (void*)w_u82u)) { printf("  FAIL: patch (UTF-16)\n");
                                                           patch_off(&p8); return 1; }
        c_hit8 = c_hitw = 0;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live8, livew, fr8, frw, &got);
            if (got.st != expected[i].st || got.len != expected[i].len ||
                got.max != expected[i].max || got.hash != expected[i].hash) {
                if (++differ <= 6)
                    printf("  differ at %ld (dir %d, alloc %d, len %d, cap %u): "
                           "live %08lX/%u/%u  ours %08lX/%u/%u%s\n",
                           i, cur_dir, cur_alloc, cur_len, cur_cap,
                           (unsigned long)expected[i].st, expected[i].len, expected[i].max,
                           (unsigned long)got.st, got.len, got.max,
                           got.hash != expected[i].hash ? "  (the destination differs)" : "");
            }
        }
        printf("  [patched]    %d cases, %ld differ (status, Length, MaximumLength and the WHOLE\n"
               "               destination);  our-code calls = %ld + %ld\n",
               NCASE, differ, (long)c_hit8, (long)c_hitw);
        OK(differ == 0, "an export answered differently under the patch");
        OK(c_hit8 + c_hitw == (LONG)NCASE, "the counters did not move once per call");
        OK(c_hit8 > 10000 && c_hitw > 10000, "one of the two directions was barely exercised");
        OK(patch_off(&p8), "the UTF-8 prologue was not restored byte-for-byte");
        OK(patch_off(&pw), "the UTF-16 prologue was not restored byte-for-byte");
    }

    {
        long post = 0;
        LONG b8 = c_hit8, bw = c_hitw;
        rec_t got;
        for (i = 0; i < NCASE; ++i) {
            build_case(i);
            run_case(live8, livew, fr8, frw, &got);
            if (got.st != expected[i].st || got.len != expected[i].len ||
                got.max != expected[i].max || got.hash != expected[i].hash) ++post;
        }
        printf("  [post]       %d cases through the RESTORED exports, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n",
               NCASE, post, (long)((c_hit8 - b8) + (c_hitw - bw)));
        OK(post == 0, "the restored exports no longer answer as they did");
        OK(c_hit8 == b8 && c_hitw == bw, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (both exports patched together, every status,\n"
                      "Length, MaximumLength and destination byte identical, every allocated block\n"
                      "freed by the UNPATCHED RtlFreeUTF8String / RtlFreeUnicodeString, and both\n"
                      "prologues restored byte-exact)\n",
           failures);
    return failures ? 1 : 0;
}
