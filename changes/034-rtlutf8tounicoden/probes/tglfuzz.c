/* changes/034-rtlutf8tounicoden/probes/tglfuzz.c
 *
 * The extra gate impl_tgl.asm needs and correctness.c does not provide.
 *
 * correctness.c is the parent's gate and is used unmodified, which is the point of a variant. But
 * it was written for the AVX2 file, and two of its properties stop it from being a sufficient gate
 * for this one:
 *
 *   1. Its lengths are short. The random fuzz draws n < 200 and the run corpora go to 200. The new
 *      block reads 64 bytes at a time and loops its decoder over every sixteen subparts, so the
 *      second, third and fourth passes of that loop, and the case where one 64-byte block does not
 *      contain the whole string, barely occur. This file runs to 8192 bytes.
 *   2. Its malformed input is either random or a single planted byte. The AVX2 file refused every
 *      malformed block and let a scalar decoder handle it, so that was enough. This one implements
 *      the maximal-subpart rule itself, in mask arithmetic -- which byte a sequence swallows, where
 *      an out-of-range second byte stops it, and that an invalid lead swallows nothing. Every one
 *      of those is a separate way to be wrong, and they have to be hit deliberately, at every
 *      offset relative to a 64-byte boundary, because the rule is computed with shifts across a
 *      64-bit mask and a bit that falls off the end of that word is a real failure mode.
 *
 * And a destination guard page. correctness.c proves nothing is written past the capacity, by
 * filling and re-checking. It cannot prove nothing is ATTEMPTED past it, and this implementation
 * writes with 512-bit masked stores whose address is computed past the end of what they write.
 * Masked stores must not fault on their masked-off elements; if that reasoning is wrong, only a
 * NO-ACCESS page after the destination will say so.
 *
 * build (from the change directory, after tools/vsenv.ps1):
 *   ml64 /nologo /c /Foprobes\pf.obj impl_tgl.asm
 *   cl /nologo /O2 probes\tglfuzz.c probes\pf.obj /Fe:probes\tglfuzz.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (WINAPI *fn)(wchar_t*, ULONG, PULONG, const void*, ULONG);
extern NTSTATUS wia_u82u(wchar_t*, ULONG, PULONG, const void*, ULONG);
static fn sys;
static long long cases = 0;
static int failures = 0;

#define MAXN 8192
static unsigned char  src[MAXN + 64];
static wchar_t d1[MAXN + 64], d2[MAXN + 64];

static void one(const unsigned char* s, int n, ULONG dbytes, const char* what)
{
    ULONG l1 = 0, l2 = 0, b, cmp;
    NTSTATUS s1, s2;
    int bad;
    ++cases;
    memset(d1, 0x23, sizeof d1);
    memset(d2, 0x23, sizeof d2);
    s1 = sys(d1, dbytes, &l1, s, (ULONG)n);
    s2 = wia_u82u(d2, dbytes, &l2, s, (ULONG)n);
    bad = (s1 != s2) || (l1 != l2);
    cmp = dbytes < sizeof d1 ? dbytes : (ULONG)sizeof d1;
    for (b = 0; b < cmp && !bad; ++b)
        if (((unsigned char*)d1)[b] != ((unsigned char*)d2)[b]) bad = 1;
    /* and nothing at or past the capacity */
    for (b = dbytes; b < (ULONG)sizeof d2 && !bad; ++b)
        if (((unsigned char*)d2)[b] != 0x23) bad = 1;
    if (bad) {
        if (++failures <= 8) {
            int i, lim = n < 80 ? n : 80;
            printf("FAIL [%s] n=%d db=%lu: ntdll %lx/%lu  ours %lx/%lu\n  src:",
                   what, n, dbytes, s1, l1, s2, l2);
            for (i = 0; i < lim; ++i) printf(" %02X", s[i]);
            printf("%s\n", n > lim ? " ..." : "");
        }
    }
}

/* one malformed sequence, chosen from every class that has its own rule */
static int plant(unsigned char* s, int k)
{
    switch (k % 22) {
    case 0:  s[0] = 0x80; return 1;                                   /* stray continuation      */
    case 1:  s[0] = 0xBF; return 1;
    case 2:  s[0] = 0xC0; s[1] = 0x80; return 2;                      /* overlong two-byte       */
    case 3:  s[0] = 0xC1; s[1] = 0xBF; return 2;
    case 4:  s[0] = 0xF5; s[1] = 0x80; s[2] = 0x80; s[3] = 0x80; return 4;   /* never a lead     */
    case 5:  s[0] = 0xFF; return 1;
    case 6:  s[0] = 0xFE; s[1] = 0x80; return 2;
    case 7:  s[0] = 0xE0; s[1] = 0x80; s[2] = 0x80; return 3;         /* overlong three-byte     */
    case 8:  s[0] = 0xE0; s[1] = 0x9F; s[2] = 0xBF; return 3;         /* ... just below the edge */
    case 9:  s[0] = 0xE0; s[1] = 0xA0; s[2] = 0x80; return 3;         /* ... the edge itself: OK */
    case 10: s[0] = 0xED; s[1] = 0xA0; s[2] = 0x80; return 3;         /* encoded surrogate       */
    case 11: s[0] = 0xED; s[1] = 0xBF; s[2] = 0xBF; return 3;
    case 12: s[0] = 0xED; s[1] = 0x9F; s[2] = 0xBF; return 3;         /* U+D7FF: OK              */
    case 13: s[0] = 0xF0; s[1] = 0x80; s[2] = 0x80; s[3] = 0x80; return 4;   /* overlong four    */
    case 14: s[0] = 0xF0; s[1] = 0x8F; s[2] = 0xBF; s[3] = 0xBF; return 4;
    case 15: s[0] = 0xF0; s[1] = 0x90; s[2] = 0x80; s[3] = 0x80; return 4;   /* U+10000: OK      */
    case 16: s[0] = 0xF4; s[1] = 0x8F; s[2] = 0xBF; s[3] = 0xBF; return 4;   /* U+10FFFF: OK     */
    case 17: s[0] = 0xF4; s[1] = 0x90; s[2] = 0x80; s[3] = 0x80; return 4;   /* past the top     */
    case 18: s[0] = 0xE2; s[1] = 0x82; return 2;                      /* truncated by what next  */
    case 19: s[0] = 0xE2; s[1] = 0x41; return 2;                      /* byte 2 not a cont       */
    case 20: s[0] = 0xF0; s[1] = 0x9F; s[2] = 0x98; return 3;         /* truncated four-byte     */
    default: s[0] = 0xC2; s[1] = 0x41; return 2;
    }
}

/* n bytes of well-formed text in a rotating mixture of widths */
static void weave(unsigned char* s, int n, unsigned* seed)
{
    int i = 0;
    while (i < n) {
        unsigned r = (*seed = *seed * 1103515245u + 12345u) >> 9;
        int w = (int)(r % 4) + 1;
        if (i + w > n) w = 1;
        switch (w) {
        case 1: s[i] = (unsigned char)(0x20 + (r % 0x5F)); break;
        case 2: s[i] = (unsigned char)(0xC2 + (r % 0x1E)); s[i+1] = (unsigned char)(0x80 + (r % 0x40)); break;
        case 3: { unsigned char lead = (unsigned char)(0xE0 + (r % 0x10));
                  unsigned char lo = 0x80, hi = 0xBF;
                  if (lead == 0xE0) lo = 0xA0; else if (lead == 0xED) hi = 0x9F;
                  s[i] = lead;
                  s[i+1] = (unsigned char)(lo + ((r >> 4) % (unsigned)(hi - lo + 1)));
                  s[i+2] = (unsigned char)(0x80 + ((r >> 8) % 0x40)); }
                break;
        default:{ unsigned char lead = (unsigned char)(0xF0 + (r % 5));
                  unsigned char lo = 0x80, hi = 0xBF;
                  if (lead == 0xF0) lo = 0x90; else if (lead == 0xF4) hi = 0x8F;
                  s[i] = lead;
                  s[i+1] = (unsigned char)(lo + ((r >> 4) % (unsigned)(hi - lo + 1)));
                  s[i+2] = (unsigned char)(0x80 + ((r >> 8) % 0x40));
                  s[i+3] = (unsigned char)(0x80 + ((r >> 14) % 0x40)); }
                break;
        }
        i += w;
    }
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    unsigned seed = 2026;
    int t, i, n, k, pos;

    sys = (fn)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    if (!sys) { printf("no export\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    /* 1. long well-formed mixtures, every length across several 64-byte blocks */
    for (n = 0; n <= 600 && failures < 8; ++n) { weave(src, n, &seed); one(src, n, 60000, "weave"); }
    for (t = 0; t < 4000 && failures < 8; ++t) {
        n = 600 + (int)((seed = seed * 1103515245u + 12345u) >> 9) % (MAXN - 600);
        weave(src, n, &seed);
        one(src, n, 60000, "weave-long");
    }
    printf("  well-formed mixtures of all four widths, to %d bytes: %lld cases\n", MAXN, cases);

    /* 2. every malformed class, planted at every offset relative to a 64-byte boundary, in text
          that is otherwise well formed. This is the one the mask shifts can get wrong. */
    {
        long long before = cases;
        for (k = 0; k < 22 && failures < 8; ++k) {
            for (pos = 0; pos < 200; ++pos) {
                int len;
                weave(src, 400, &seed);
                /* re-plant on a clean ASCII bed too, so the neighbourhood varies */
                if (pos & 1) for (i = 0; i < 400; ++i) src[i] = (unsigned char)('a' + (i % 26));
                len = plant(src + pos, k);
                (void)len;
                one(src, 400, 60000, "planted");
                one(src, pos + 8, 60000, "planted-at-end");
            }
        }
        printf("  every malformed class at every offset 0..199, two beds: %lld cases\n", cases - before);
    }

    /* 3. runs of one malformed class, so a whole 64-byte block is malformed */
    {
        long long before = cases;
        for (k = 0; k < 22 && failures < 8; ++k) {
            for (n = 0; n <= 300; ++n) {
                int len = 0;
                while (len < n - 4) len += plant(src + len, k);
                while (len < n) src[len++] = (unsigned char)('a' + (len % 26));
                one(src, n, 60000, "run");
                if ((n & 7) == 0) one(src, n, (ULONG)n, "run-tight");
            }
        }
        printf("  runs of each malformed class, every length to 300: %lld cases\n", cases - before);
    }

    /* 4. fully random bytes at lengths that span many blocks, and at tight capacities */
    {
        long long before = cases;
        for (t = 0; t < 40000 && failures < 8; ++t) {
            n = (int)((seed = seed * 1103515245u + 12345u) >> 9) % 1500;
            for (i = 0; i < n; ++i) {
                unsigned r = (seed = seed * 1103515245u + 12345u) >> 8;
                int p = r % 10;
                if (p < 5)      src[i] = (unsigned char)(r % 0x80);
                else if (p < 7) src[i] = (unsigned char)(0xC0 + (r % 0x40));
                else if (p < 8) src[i] = (unsigned char)(0xE0 + (r % 0x10));
                else if (p < 9) src[i] = (unsigned char)(0xF0 + (r % 8));
                else            src[i] = (unsigned char)(0x80 + (r % 0x40));
            }
            one(src, n, 60000, "random");
            if ((t & 3) == 0) one(src, n, (ULONG)(2 * n + 2), "random-exact");
            if ((t & 7) == 0) one(src, n, (ULONG)(t % (n ? 2 * n + 3 : 4)), "random-tight");
        }
        printf("  fully random bytes to 1500, generous / exact / tight capacities: %lld cases\n",
               cases - before);
    }

    /* 5. The destination against a no-access page. 512-bit masked stores compute an address past
          what they write; if a masked-off element could fault, this is where it shows. */
    {
        SYSTEM_INFO si;
        unsigned char* base;
        int caught = 0, len;
        long long before = cases;
        GetSystemInfo(&si);
        base = (unsigned char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (base && VirtualAlloc(base, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) {
            for (k = 0; k < 22; ++k) {
                for (len = 1; len <= 400; ++len) {
                    ULONG cap;
                    int done = 0;
                    while (done < len - 4) done += plant(src + done, (k + done) % 22);
                    while (done < len) { src[done] = (unsigned char)('a' + (done % 26)); ++done; }
                    /* every capacity that ends exactly at the page edge */
                    for (cap = 0; cap <= 64; cap += 2) {
                        wchar_t* d = (wchar_t*)(base + si.dwPageSize - cap);
                        ULONG got = 0;
                        __try { wia_u82u(d, cap, &got, src, (ULONG)len); }
                        __except (EXCEPTION_EXECUTE_HANDLER) {
                            if (++caught <= 4)
                                printf("FAIL k=%d len=%d cap=%lu: a WRITE went past the destination\n",
                                       k, len, cap);
                            ++failures;
                        }
                        ++cases;
                    }
                }
            }
            printf("  the destination ending at a NO-ACCESS page, 22 classes x 400 lengths x 33\n"
                   "      capacities: %lld cases, %d faults\n", cases - before, caught);
        } else {
            printf("  the destination guard-page test could not allocate; SKIPPED\n");
            ++failures;
        }
    }

    /* 6. and the SOURCE against a NO-ACCESS page at every length the wide load can see */
    {
        SYSTEM_INFO si;
        unsigned char* base;
        int caught = 0, len;
        long long before = cases;
        GetSystemInfo(&si);
        base = (unsigned char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (base && VirtualAlloc(base, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) {
            for (k = 0; k < 22; ++k) {
                for (len = 0; len <= 200; ++len) {
                    unsigned char* p = base + si.dwPageSize - len;
                    int done = 0;
                    while (done < len - 4) done += plant(src + done, (k + done) % 22);
                    while (done < len) { src[done] = (unsigned char)('a' + (done % 26)); ++done; }
                    memcpy(p, src, (size_t)len);
                    __try { one(p, len, 60000, "src-guard"); }
                    __except (EXCEPTION_EXECUTE_HANDLER) {
                        if (++caught <= 4)
                            printf("FAIL k=%d len=%d: a READ went past the source\n", k, len);
                        ++failures;
                    }
                }
            }
            printf("  the source ending at a NO-ACCESS page, 22 classes x 201 lengths: %lld cases,"
                   " %d over-reads\n", cases - before, caught);
        } else {
            printf("  the source guard-page test could not allocate; SKIPPED\n");
            ++failures;
        }
    }

    printf(failures ? "TGLFUZZ: FAIL (%d)\n" : "TGLFUZZ: PASS (%lld cases)\n",
           failures ? failures : (int)0);
    if (!failures) printf("  total %lld cases, 0 disagreements with the live export\n", cases);
    return failures ? 1 : 0;
}
