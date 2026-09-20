// live-substitution/live_subst_inetaddr.c
// LIVE-RUN PROOF for change 273 (ws2_32!inet_addr).
//
// This harness exists because the repository already claimed this export was covered, and the claim
// Was too strong. live_subst_ws2.c patches ntdll!RtlIpv4StringToAddressA (change 114), calls
// ws2_32!inet_addr, and shows the counter moving on every input -- which proves inet_addr DELEGATES
// to it, and it does, on all 22 of its subjects. What it does not prove is the headline above it:
// "inet_addr does not parse an address at all". changes/273-inet-addr/probes/grammar.c asks both
// functions the same ten questions and they disagree on six:
//
//     "1.2"        inet_addr 02000001     Rtl refuses (STATUS_INVALID_PARAMETER)
//     "1"          inet_addr 01000000     Rtl refuses
//     "0x7f.1"     inet_addr 0100007F     Rtl refuses
//     "010.1.1.1"  inet_addr 01010108     Rtl refuses
//     "1.2.3.4x"   inet_addr refuses      Rtl 04030201
//
// So inet_addr calls RtlIpv4StringToAddressA and then, when that refuses, parses the string ITSELF
// with a far more permissive grammar -- four forms, three bases, a wrapping accumulator and a
// whitespace terminator. The delegation is real; the coverage was not. The other harness's counter
// could never have caught that, because patching change 114 with a bit-exact replacement leaves the
// composite answer unchanged whichever branch inet_addr takes.
//
// So this one patches ws2_32!inet_addr ITSELF.
//
// The corpus is built from what the probes found, not from plausible addresses: the wrapping
// accumulator (which no description of inet_addr contains), the whitespace terminator, every field
// boundary in all three bases, and the single byte 0x20 that is an address on its own. A corpus of
// realistic dotted quads exercises none of it.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded, patching only its own copy-on-write copy.
//   (1) Validate first against the live export before any patch exists.
//   (2) Patch only when idle: single-threaded, and inet_addr is used by neither loader nor heap.
//   (3) REVERSIBLE: the original bytes are restored, VERIFIED byte-for-byte, and the corpus re-run.
//
// Build: build_inetaddr_live.bat
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

#pragma comment(lib, "ws2_32.lib")

typedef unsigned long (WSAAPI *F_IA)(const char*);

extern unsigned long wia_inet_addr(const char*);

static volatile LONG c_hit;
static unsigned long WSAAPI w_ia(const char* s)
{ _InterlockedIncrement(&c_hit); return wia_inet_addr(s); }

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
static unsigned long expected[NCASE];
static char cur[64];
static int cur_cls;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void put(int* n, const char* s) { while (*s) cur[(*n)++] = *s++; }
static void putnum(int* n, unsigned long long v, int base, int prefix)
{
    char t[32];
    int k = 0;
    if (prefix == 16) { cur[(*n)++] = '0'; cur[(*n)++] = (rnd() & 1) ? 'x' : 'X'; }
    else if (prefix == 8) cur[(*n)++] = '0';
    if (!v) t[k++] = '0';
    while (v) { unsigned d = (unsigned)(v % (unsigned)base); t[k++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= (unsigned)base; }
    while (k) cur[(*n)++] = t[--k];
}

static const unsigned char WS[] = { 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20 };

/* The case is a pure function of its index. Change 252's harness carried prng state across its
   passes and reported 14285 differences with its patch counter at ZERO. */
static void build_case(long i)
{
    int n = 0, k, parts, base, prefix;
    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;
    cur_cls = (int)(i % 7);

    switch (cur_cls) {
    case 0:                                       /* dotted quads, valid and just over */
    case 1:
        parts = 1 + (int)(rnd() % 4);
        for (k = 0; k < parts; ++k) {
            unsigned long long v;
            if (k) cur[n++] = '.';
            switch (rnd() % 4) {
            case 0:  v = rnd() % 256; break;
            case 1:  v = 254 + rnd() % 4; break;   /* straddles the 8-bit field */
            case 2:  v = 65534 + rnd() % 4; break; /* ... the 16-bit one */
            default: v = 16777214 + rnd() % 4; break;
            }
            base = (rnd() % 3 == 0) ? 8 : ((rnd() % 3 == 0) ? 16 : 10);
            prefix = (base == 10) ? 0 : base;
            putnum(&n, v, base, prefix);
        }
        break;

    case 2: {                                     /* THE WRAPPING ACCUMULATOR: 9..20 digits */
        int digits = 9 + (int)(rnd() % 12);
        base = (rnd() % 3 == 0) ? 8 : ((rnd() % 3 == 0) ? 16 : 10);
        if (base == 16) { cur[n++] = '0'; cur[n++] = 'x'; }
        else if (base == 8) cur[n++] = '0';
        for (k = 0; k < digits; ++k) {
            unsigned d = rnd() % (unsigned)base;
            cur[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        }
        if (rnd() & 1) { cur[n++] = '.'; putnum(&n, rnd() % 300, 10, 0); }
        break;
    }

    case 3:                                       /* the whitespace terminator, everywhere */
        put(&n, "1.22.33.44");
        { int at = (int)(rnd() % (unsigned)(n + 1));
          int m = n;
          for (k = m; k > at; --k) cur[k] = cur[k - 1];
          cur[at] = (char)WS[rnd() % 6];
          ++n; }
        if (rnd() & 1) put(&n, "junk after");
        break;

    case 4:                                       /* leading zeros, unbounded */
        { int z = (int)(rnd() % 30);
          for (k = 0; k < z; ++k) cur[n++] = '0';
          put(&n, "1.2.3.4"); }
        break;

    case 5:                                       /* one byte changed, anywhere */
        put(&n, "1.2.3.4");
        cur[rnd() % (unsigned)n] = (char)(1 + rnd() % 255);
        break;

    default:                                      /* random over the grammar's alphabet */
        { int len = 1 + (int)(rnd() % 20);
          for (k = 0; k < len; ++k) {
            if ((rnd() & 15) == 0) cur[n++] = (char)(1 + rnd() % 255);
            else cur[n++] = "0123456789.xX abcdefABCDEF\t"[rnd() % 27];
          } }
        break;
    }
    if (n == 0) { cur[n++] = ' '; }               /* the one-byte special case, on purpose */
    cur[n] = 0;
}

int main(void)
{
    WSADATA wd;
    HMODULE hw;
    F_IA live;
    patch_t pt;
    long i;
    long n_ok = 0, n_none = 0, n_wrap = 0, n_ws = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wd);
    hw = LoadLibraryW(L"ws2_32.dll");
    live = (F_IA)GetProcAddress(hw, "inet_addr");
    if (!live) { printf("resolve failed\n"); return 1; }
    printf("== LIVE SUBSTITUTION: ws2_32!inet_addr (change 273) ==\n");
    printf("  the export resolves to %p\n", (void*)live);

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        expected[i] = live(cur);
        if (expected[i] == INADDR_NONE) ++n_none; else ++n_ok;
        if (cur_cls == 2) ++n_wrap;
        if (cur_cls == 3) ++n_ws;
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export;  accepted %ld, refused %ld;\n"
           "               %ld took the WRAPPING ACCUMULATOR class (9..20 digits) and %ld the\n"
           "               WHITESPACE TERMINATOR class\n", NCASE, n_ok, n_none, n_wrap, n_ws);
    OK(n_ok   > 5000, "the corpus rarely produced an address");
    OK(n_none > 5000, "the corpus rarely produced a refusal");
    OK(n_wrap > 3000, "the corpus barely reached the wrapping accumulator -- the rule no\n"
                      "        description of inet_addr contains");
    OK(n_ws   > 3000, "the corpus barely reached the whitespace terminator");

    {
        long differ = 0;
        if (!patch_on(&pt, (void*)live, (void*)w_ia)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            unsigned long got;
            build_case(i);
            got = live(cur);
            if (got != expected[i]) {
                if (++differ <= 8)
                    printf("  differ at %ld (class %d, \"%.40s\"): live %08lX  ours %08lX\n",
                           i, cur_cls, cur, (unsigned long)expected[i], (unsigned long)got);
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
            if (live(cur) != expected[i]) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  "
               "our-code calls = %ld (must not have moved)\n",
               NCASE, post, (long)(c_hit - before));
        OK(post == 0, "the restored export no longer answers as it did");
        OK(c_hit == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (the 32-bit result identical over %d cases,\n"
                      "including the wrapping accumulator and the whitespace terminator that\n"
                      "ws2_32!inet_addr does NOT get from ntdll; prologue restored byte-exact and\n"
                      "the corpus re-run through it)\n",
           failures ? failures : NCASE);
    return failures ? 1 : 0;
}
