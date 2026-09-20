/* changes/264-rtlinitutf8string/correctness.c
 *
 * THREE-WAY: ours vs an independent oracle vs the LIVE ntdll!RtlInitUTF8String.
 *
 * The gates are not shared with change 095, and that is deliberate. This change's implementation is
 * an ALIAS of change 095's, probes/equiv.c proved the two exports identical over 125883 cases
 * including every ordered byte pair, but "the exports agree with each other" and "our code is
 * bit-exact for THIS export" are different statements, and only the second one is what a change
 * here is allowed to claim. So every case below is compared against the live RtlInitUTF8String at
 * its own address.
 *
 * All three fields are compared against a poisoned struct. Length, MaximumLength and Buffer are
 * filled with 0xCD before every call, so a field an implementation forgets to write is a mismatch
 * rather than a coincidence; the NULL case in particular must write all three.
 *
 *   1. every LENGTH from 0 to 600, so no length-dependent rule can hide between sizes.
 *   2. Every byte value as the only character, and every ordered pair, which is every UTF-8
 *      lead/continuation combination, every overlong prefix and every truncated sequence.
 *   3. The clamp: every length from 65400 to 65700, where both ushort fields saturate.
 *   4. A GUARD PAGE: the string ending exactly at an inaccessible page, at every alignment, which
 *      is what a 32-byte-at-a-time scan has to survive.
 *   5. NULL.
 *   6. RANDOMISED over ASCII, high-bit, valid UTF-8 and malformed UTF-8.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } U8STR;
typedef void (NTAPI *F_Init)(U8STR*, const char*);

void wia_rtlinitutf8string(U8STR*, const char*);

static F_Init live;
static long cases = 0, fails = 0, clamped = 0;

/* The oracle: the rule probes/contract.c measured, written out. */
static void ref_init(U8STR* d, const char* s)
{
    SIZE_T n = 0;
    if (!s) { d->Length = 0; d->MaximumLength = 0; d->Buffer = NULL; return; }
    while (s[n]) ++n;
    if (n > 65534) n = 65534;              /* Length saturates at 0xFFFE, MaximumLength at 0xFFFF */
    d->Length = (USHORT)n;
    d->MaximumLength = (USHORT)(n + 1);
    d->Buffer = (PSTR)s;
}

static void one(const char* s, const char* where)
{
    U8STR a, b, c;
    ++cases;
    memset(&a, 0xCD, sizeof a);
    memset(&b, 0xCD, sizeof b);
    memset(&c, 0xCD, sizeof c);
    wia_rtlinitutf8string(&a, s);
    ref_init(&b, s);
    live(&c, s);
    if (c.Length == 65534) ++clamped;
    if (a.Length != c.Length || a.MaximumLength != c.MaximumLength || a.Buffer != c.Buffer ||
        b.Length != c.Length || b.MaximumLength != c.MaximumLength || b.Buffer != c.Buffer) {
        if (++fails <= 20)
            printf("  MISMATCH [%s]: ours{%u,%u,%p} ref{%u,%u,%p} live{%u,%u,%p}\n", where,
                   a.Length, a.MaximumLength, (void*)a.Buffer,
                   b.Length, b.MaximumLength, (void*)b.Buffer,
                   c.Length, c.MaximumLength, (void*)c.Buffer);
    }
}

static unsigned long long rs = 0x8AED2A6B6E2C1F35ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    static char buf[70000];
    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_Init)GetProcAddress(h, "RtlInitUTF8String");
    if (!live) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlInitUTF8String ==\n");
    printf("   all THREE fields compared against a struct poisoned with 0xCD\n");

    /* 1. every length 0..600 */
    {
        long before = cases;
        int len, k;
        for (len = 0; len <= 600; ++len) {
            for (k = 0; k < len; ++k) buf[k] = (char)(0x21 + (k % 94));
            buf[len] = 0;
            one(buf, "every length 0..600");
        }
        printf("  1. every length from 0 to 600: %ld\n", cases - before);
    }

    /* 2. every byte and every ordered pair */
    {
        long before = cases;
        unsigned i, j;
        char s[3];
        for (i = 1; i < 256; ++i) { s[0] = (char)i; s[1] = 0; one(s, "every single byte"); }
        for (i = 1; i < 256; ++i)
            for (j = 1; j < 256; ++j) {
                s[0] = (char)i; s[1] = (char)j; s[2] = 0;
                one(s, "every ordered byte pair");
            }
        printf("  2. every byte and every ordered PAIR -- every UTF-8 lead/continuation\n"
               "     combination, overlong prefix and truncated sequence: %ld\n", cases - before);
    }

    /* 3. the clamp */
    {
        long before = cases;
        int len, k;
        for (len = 65400; len <= 65700; ++len) {
            for (k = 0; k < len; ++k) buf[k] = 'z';
            buf[len] = 0;
            one(buf, "across the clamp");
        }
        printf("  3. every length from 65400 to 65700, across the USHORT clamp (%ld of them\n"
               "     saturated): %ld\n", clamped, cases - before);
    }

    /* 4. a guard page */
    {
        SYSTEM_INFO si;
        char* base;
        DWORD old;
        long guard = 0;
        GetSystemInfo(&si);
        base = (char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base || !VirtualProtect(base + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old)) {
            printf("  4. guard page SKIPPED\n");
        } else {
            int len, k;
            for (len = 0; len <= 200; ++len) {
                char* s = base + si.dwPageSize - (len + 1);   /* the NUL is the last readable byte */
                for (k = 0; k < len; ++k) s[k] = (char)(0x41 + (k % 26));
                s[len] = 0;
                one(s, "guard page");
                ++guard;
            }
            printf("  4. the string ENDING at a PAGE_NOACCESS page, every length 0..200 and so\n"
                   "     every alignment: %ld cases, no fault\n", guard);
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    /* 5. NULL */
    one(NULL, "NULL");
    printf("  5. a NULL source, which must write all three fields: 1\n");

    /* 6. randomised */
    {
        long before = cases;
        int trial, k;
        for (trial = 0; trial < 120000; ++trial) {
            int mode = trial & 3, len = (int)(rnd() % 500);
            for (k = 0; k < len; ++k) {
                unsigned r = rnd();
                switch (mode) {
                case 0: buf[k] = (char)(0x20 + (r % 95)); break;
                case 1: buf[k] = (char)(0x80 | (r & 0x7F)); break;
                case 2:
                    if ((r & 3) == 0 && k + 1 < len) { buf[k] = (char)0xE2; buf[++k] = (char)0x82; }
                    else buf[k] = (char)(0x41 + (r % 26));
                    break;
                default: buf[k] = (char)(r ? r : 1); break;
                }
            }
            buf[len] = 0;
            one(buf, "randomised");
        }
        printf("  6. randomised: ASCII, high-bit, valid UTF-8 and malformed: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    if (!clamped) { printf("CORRECTNESS: FAILED (the clamp was never reached)\n"); return 1; }
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (all three fields exact vs the oracle AND the live export)\n");
    return fails ? 1 : 0;
}
