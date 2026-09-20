/* changes/268-rtlunicodestringtoutf8string/correctness.c
 *
 * THREE-WAY: ours vs the LIVE ntdll exports, for both directions.
 *
 * There is no separate oracle here, and that is deliberate rather than a shortcut: the conversion
 * itself is changes 016 and 034, each already gated bit-exact against its own live N-form over its
 * own corpora. What is new in THIS change is the wrapper -- the capacity arithmetic, the
 * terminator, the Length, and which of two failure statuses comes back -- so the corpora below are
 * built to exercise exactly that, and they compare against the live wrapper, which is the only
 * thing that can confirm it.
 *
 * What is compared, on every case: the NTSTATUS, Length, MaximumLength, and the whole destination
 * buffer against a poison fill. The buffer matters because on failure the destination is partially
 * WRITTEN -- "abc" into MaximumLength 2 leaves an 'a' behind -- and an implementation that tidied
 * that up, or wrote one byte more, would pass any test that only read the status.
 *
 *   1. every source length 0..40 against every capacity 0..48, both directions. This is the table
 *      probes/statuses.c drew by hand, enumerated: it pins the terminator rule, the two different
 *      failure codes, and the partial write, at every relationship between the two sizes.
 *   2. MULTI-BYTE output, where the byte count and the character count are different numbers, at
 *      every capacity -- two-, three- and four-byte sequences, and a surrogate pair.
 *   3. Invalid input: lone surrogates going out, malformed UTF-8 coming in. Both are replaced, and
 *      the call still succeeds with STATUS_SOME_NOT_MAPPED.
 *   4. The allocating path, whose block must be the right size and must be freeable by the paired
 *      RtlFreeUTF8String.
 *   5. RANDOMISED lengths, contents and capacities.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG (NTAPI *F_U2U8)(U8STR*, const USTR*, BOOLEAN);
typedef LONG (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);
typedef void (NTAPI *F_FreeU8)(U8STR*);
typedef void (NTAPI *F_FreeU)(USTR*);

LONG wia_unicodestringtoutf8string(U8STR*, const USTR*, BOOLEAN);
LONG wia_utf8stringtounicodestring(USTR*, const U8STR*, BOOLEAN);

static F_U2U8 l_u2u8;
static F_U82U l_u82u;
static F_FreeU8 l_freeu8;
static F_FreeU  l_freeu;

static long cases = 0, fails = 0, n_ok = 0, n_toosmall = 0, n_overflow = 0, n_notmapped = 0;

#define CAP 256
static char  o_buf[CAP], l_buf[CAP];
static wchar_t ow_buf[CAP], lw_buf[CAP];

static void one8(const wchar_t* s, int wlen, USHORT maxlen, const char* where)
{
    USTR in;
    U8STR a, b;
    LONG ro, rl;
    ++cases;
    memset(o_buf, '#', CAP);
    memset(l_buf, '#', CAP);
    in.Buffer = (PWSTR)s; in.Length = (USHORT)(wlen * 2); in.MaximumLength = in.Length;
    a.Buffer = o_buf; a.Length = 0xBEEF; a.MaximumLength = maxlen;
    b.Buffer = l_buf; b.Length = 0xBEEF; b.MaximumLength = maxlen;
    ro = wia_unicodestringtoutf8string(&a, &in, FALSE);
    rl = l_u2u8(&b, &in, FALSE);
    if (rl == 0) ++n_ok;
    else if ((ULONG)rl == 0xC0000023ul) ++n_toosmall;
    else if ((ULONG)rl == 0x80000005ul) ++n_overflow;
    if (rl == 0x107) ++n_notmapped;
    if (ro != rl || a.Length != b.Length || a.MaximumLength != b.MaximumLength ||
        memcmp(o_buf, l_buf, CAP) != 0) {
        if (++fails <= 20) {
            int k;
            printf("  MISMATCH [%s->UTF8 %s] wlen=%d max=%u: status ours=%08lX live=%08lX  "
                   "Length ours=%u live=%u", where, "", wlen, maxlen,
                   (unsigned long)ro, (unsigned long)rl, a.Length, b.Length);
            for (k = 0; k < CAP; ++k)
                if (o_buf[k] != l_buf[k]) {
                    printf("   first differing byte [%d]: ours=%02X live=%02X",
                           k, (unsigned char)o_buf[k], (unsigned char)l_buf[k]);
                    break;
                }
            printf("\n");
        }
    }
}

static void oneW(const char* s, int blen, USHORT maxlen, const char* where)
{
    U8STR in;
    USTR a, b;
    LONG ro, rl;
    ++cases;
    memset(ow_buf, 0x23, sizeof ow_buf);
    memset(lw_buf, 0x23, sizeof lw_buf);
    in.Buffer = (PSTR)s; in.Length = (USHORT)blen; in.MaximumLength = (USHORT)blen;
    a.Buffer = ow_buf; a.Length = 0xBEEF; a.MaximumLength = maxlen;
    b.Buffer = lw_buf; b.Length = 0xBEEF; b.MaximumLength = maxlen;
    ro = wia_utf8stringtounicodestring(&a, &in, FALSE);
    rl = l_u82u(&b, &in, FALSE);
    if (rl == 0) ++n_ok;
    else if ((ULONG)rl == 0xC0000023ul) ++n_toosmall;
    else if ((ULONG)rl == 0x80000005ul) ++n_overflow;
    if (rl == 0x107) ++n_notmapped;
    if (ro != rl || a.Length != b.Length || a.MaximumLength != b.MaximumLength ||
        memcmp(ow_buf, lw_buf, sizeof ow_buf) != 0) {
        if (++fails <= 20) {
            int k;
            printf("  MISMATCH [%s->UTF16] blen=%d max=%u: status ours=%08lX live=%08lX  "
                   "Length ours=%u live=%u", where, blen, maxlen,
                   (unsigned long)ro, (unsigned long)rl, a.Length, b.Length);
            for (k = 0; k < CAP; ++k)
                if (ow_buf[k] != lw_buf[k]) {
                    printf("   first differing word [%d]: ours=%04X live=%04X",
                           k, (unsigned)ow_buf[k], (unsigned)lw_buf[k]);
                    break;
                }
            printf("\n");
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * The allocating path, both directions, any content.
 *
 * The original section 4 asked only the UTF-16 -> UTF-8 direction with ASCII, which is exactly the
 * combination that hides the two differences probes/notmapped.c found: this direction passes
 * STATUS_SOME_NOT_MAPPED through and the other SWALLOWS it, and ASCII never produces that status
 * in either. So both directions are asked here, with valid, multi-byte and malformed content.
 * ------------------------------------------------------------------------------------------- */
static void alloc8(const wchar_t* s, int wlen, const char* where)
{
    USTR in;
    U8STR a, b;
    LONG ro, rl;
    ++cases;
    in.Buffer = (PWSTR)s; in.Length = (USHORT)(wlen * 2); in.MaximumLength = in.Length;
    memset(&a, 0xCD, sizeof a);
    memset(&b, 0xCD, sizeof b);
    ro = wia_unicodestringtoutf8string(&a, &in, TRUE);
    rl = l_u2u8(&b, &in, TRUE);
    if (rl == 0x107) ++n_notmapped;
    if (ro != rl || a.Length != b.Length || a.MaximumLength != b.MaximumLength ||
        (rl >= 0 && memcmp(a.Buffer, b.Buffer, (size_t)a.Length + 1) != 0)) {
        if (++fails <= 20)
            printf("  MISMATCH [allocating %s->UTF8] wlen=%d: ours %08lX/%u/%u  live %08lX/%u/%u\n",
                   where, wlen, (unsigned long)ro, a.Length, a.MaximumLength,
                   (unsigned long)rl, b.Length, b.MaximumLength);
    }
    if (ro >= 0 && l_freeu8) l_freeu8(&a);
    if (rl >= 0 && l_freeu8) l_freeu8(&b);
}

static void allocW(const char* s, int blen, const char* where)
{
    U8STR in;
    USTR a, b;
    LONG ro, rl;
    ++cases;
    in.Buffer = (PSTR)s; in.Length = (USHORT)blen; in.MaximumLength = (USHORT)blen;
    memset(&a, 0xCD, sizeof a);
    memset(&b, 0xCD, sizeof b);
    ro = wia_utf8stringtounicodestring(&a, &in, TRUE);
    rl = l_u82u(&b, &in, TRUE);
    if (rl == 0x107) ++n_notmapped;
    if (ro != rl || a.Length != b.Length || a.MaximumLength != b.MaximumLength ||
        (rl >= 0 && memcmp(a.Buffer, b.Buffer, (size_t)a.Length + 2) != 0)) {
        if (++fails <= 20)
            printf("  MISMATCH [allocating %s->UTF16] blen=%d: ours %08lX/%u/%u  live %08lX/%u/%u\n",
                   where, blen, (unsigned long)ro, a.Length, a.MaximumLength,
                   (unsigned long)rl, b.Length, b.MaximumLength);
    }
    if (ro >= 0 && l_freeu) l_freeu(&a);
    if (rl >= 0 && l_freeu) l_freeu(&b);
}

/* ---------------------------------------------------------------------------------------------
 * The ushort boundary, where the result is too big for the Length field and the answer is neither
 * of the two shortfall codes but STATUS_INVALID_PARAMETER_2 (probes/limits.c). Nothing in the
 * small corpora above can reach it -- a 40-character source cannot produce 65535 bytes -- and it
 * is the one case where getting it wrong on the ALLOCATING path allocates a wrapped, far too small
 * block and converts a large string into it.
 * ------------------------------------------------------------------------------------------- */
#define BIGW   32767                 /* the most characters a UNICODE_STRING can hold */
#define BIGU   40000
#define BIGOUT 200000
static wchar_t big_w[BIGW + 4];
static char    big_u[BIGU + 4];
static char    big_o8[BIGOUT], big_l8[BIGOUT];
static wchar_t big_ow[BIGOUT], big_lw[BIGOUT];
static long n_toobig = 0;

/* a source whose UTF-8 output is exactly `want` bytes, built the way probes/limits.c builds it:
   the character count is pinned at the maximum and characters are PROMOTED to raise the byte
   count, because want/2 two-byte characters would overflow the source's own Length field */
static int build_out(int want)
{
    const int n = BIGW;
    int extra = want - n, k3, i;
    if (extra < 0 || extra > 2 * n) return 0;
    k3 = extra / 2;
    for (i = 0; i < k3; ++i) big_w[i] = 0x20AC;
    if (extra & 1) big_w[k3++] = 0x00E9;
    for (i = k3; i < n; ++i) big_w[i] = L'a';
    return n;
}

static void big8(int want, int cap, const char* where)
{
    USTR in;
    U8STR a, b;
    LONG ro, rl;
    int n = build_out(want);
    ++cases;
    memset(big_o8, '#', BIGOUT);
    memset(big_l8, '#', BIGOUT);
    in.Buffer = big_w; in.Length = (USHORT)(n * 2); in.MaximumLength = in.Length;
    a.Buffer = big_o8; a.Length = 0xBEEF; a.MaximumLength = (USHORT)cap;
    b.Buffer = big_l8; b.Length = 0xBEEF; b.MaximumLength = (USHORT)cap;
    ro = wia_unicodestringtoutf8string(&a, &in, FALSE);
    rl = l_u2u8(&b, &in, FALSE);
    if ((ULONG)rl == 0xC00000F0ul) ++n_toobig;
    if (ro != rl || a.Length != b.Length || a.MaximumLength != b.MaximumLength ||
        memcmp(big_o8, big_l8, BIGOUT) != 0) {
        if (++fails <= 20)
            printf("  MISMATCH [%s, %d bytes out, max=%d]: ours %08lX/%u  live %08lX/%u%s\n",
                   where, want, cap, (unsigned long)ro, a.Length, (unsigned long)rl, b.Length,
                   memcmp(big_o8, big_l8, BIGOUT) ? "  (buffers differ)" : "");
    }
    /* and the same source through the allocating path, where a wrapped size would be a heap
       overrun rather than a wrong status */
    {
        U8STR c, d;
        LONG rc, rd;
        ++cases;
        memset(&c, 0xCD, sizeof c);
        memset(&d, 0xCD, sizeof d);
        rc = wia_unicodestringtoutf8string(&c, &in, TRUE);
        rd = l_u2u8(&d, &in, TRUE);
        if (rc != rd || c.Length != d.Length || c.MaximumLength != d.MaximumLength ||
            (rd >= 0 && memcmp(c.Buffer, d.Buffer, (size_t)c.Length + 1) != 0)) {
            if (++fails <= 20)
                printf("  MISMATCH [%s allocating, %d bytes out]: ours %08lX/%u/%u  live %08lX/%u/%u\n",
                       where, want, (unsigned long)rc, c.Length, c.MaximumLength,
                       (unsigned long)rd, d.Length, d.MaximumLength);
        }
        if (rc >= 0 && l_freeu8) l_freeu8(&c);
        if (rd >= 0 && l_freeu8) l_freeu8(&d);
    }
}

static void bigW(int n, int cap, const char* where)
{
    U8STR in;
    USTR a, b;
    LONG ro, rl;
    int i;
    ++cases;
    for (i = 0; i < n; ++i) big_u[i] = 'a';
    for (i = 0; i < BIGOUT; ++i) { big_ow[i] = 0x2323; big_lw[i] = 0x2323; }
    in.Buffer = big_u; in.Length = (USHORT)n; in.MaximumLength = (USHORT)n;
    a.Buffer = big_ow; a.Length = 0xBEEF; a.MaximumLength = (USHORT)cap;
    b.Buffer = big_lw; b.Length = 0xBEEF; b.MaximumLength = (USHORT)cap;
    ro = wia_utf8stringtounicodestring(&a, &in, FALSE);
    rl = l_u82u(&b, &in, FALSE);
    if ((ULONG)rl == 0xC00000F0ul) ++n_toobig;
    if (ro != rl || a.Length != b.Length || a.MaximumLength != b.MaximumLength ||
        memcmp(big_ow, big_lw, BIGOUT * sizeof(wchar_t)) != 0) {
        if (++fails <= 20)
            printf("  MISMATCH [%s, %d bytes in, max=%d]: ours %08lX/%u  live %08lX/%u%s\n",
                   where, n, cap, (unsigned long)ro, a.Length, (unsigned long)rl, b.Length,
                   memcmp(big_ow, big_lw, BIGOUT * sizeof(wchar_t)) ? "  (buffers differ)" : "");
    }
    {
        USTR c, d;
        LONG rc, rd;
        ++cases;
        memset(&c, 0xCD, sizeof c);
        memset(&d, 0xCD, sizeof d);
        rc = wia_utf8stringtounicodestring(&c, &in, TRUE);
        rd = l_u82u(&d, &in, TRUE);
        if (rc != rd || c.Length != d.Length || c.MaximumLength != d.MaximumLength ||
            (rd >= 0 && memcmp(c.Buffer, d.Buffer, (size_t)c.Length + 2) != 0)) {
            if (++fails <= 20)
                printf("  MISMATCH [%s allocating, %d bytes in]: ours %08lX/%u/%u  live %08lX/%u/%u\n",
                       where, n, (unsigned long)rc, c.Length, c.MaximumLength,
                       (unsigned long)rd, d.Length, d.MaximumLength);
        }
        if (rc >= 0 && l_freeu) l_freeu(&c);
        if (rd >= 0 && l_freeu) l_freeu(&d);
    }
}

static unsigned long long rs = 0x1F83D9ABFB41BD6Bull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    static wchar_t wsrc[64];
    static char    asrc[64];
    int n, cap, i;
    setvbuf(stdout, NULL, _IONBF, 0);
    l_u2u8   = (F_U2U8)  GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    l_u82u   = (F_U82U)  GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    l_freeu8 = (F_FreeU8)GetProcAddress(h, "RtlFreeUTF8String");
    l_freeu  = (F_FreeU) GetProcAddress(h, "RtlFreeUnicodeString");
    if (!l_u2u8 || !l_u82u) { printf("resolve failed\n"); return 1; }

    printf("== CORRECTNESS: RtlUnicodeStringToUTF8String / RtlUTF8StringToUnicodeString ==\n");
    printf("   status, Length, MaximumLength AND the whole destination buffer are compared,\n");
    printf("   because a FAILED call still writes part of it\n");

    /* 1. the whole size table, enumerated */
    {
        long before = cases;
        for (n = 0; n <= 40; ++n) {
            for (i = 0; i < n; ++i) { wsrc[i] = (wchar_t)(L'a' + (i % 26)); asrc[i] = (char)('a' + (i % 26)); }
            for (cap = 0; cap <= 48; ++cap) {
                one8(wsrc, n, (USHORT)cap, "ASCII");
                oneW(asrc, n, (USHORT)cap, "ASCII");
            }
        }
        printf("  1. every source length 0..40 against every capacity 0..48, both directions --\n"
               "     the terminator rule, the two failure codes and the partial write: %ld\n",
               cases - before);
    }

    /* 2. multi-byte, where bytes and characters are different numbers */
    {
        long before = cases;
        static const wchar_t MB[4] = { 0x00E9, 0x20AC, 0xD83D, 0xDE00 };   /* 2, 3, and a pair */
        for (n = 1; n <= 4; ++n)
            for (cap = 0; cap <= 24; ++cap) {
                for (i = 0; i < n; ++i) wsrc[i] = MB[i];
                one8(wsrc, n, (USHORT)cap, "multi-byte");
            }
        /* and the same sequences coming back the other way */
        {
            static const char U8[10] = { (char)0xC3,(char)0xA9, (char)0xE2,(char)0x82,(char)0xAC,
                                         (char)0xF0,(char)0x9F,(char)0x98,(char)0x80, 0 };
            for (n = 1; n <= 9; ++n)
                for (cap = 0; cap <= 24; ++cap)
                    oneW(U8, n, (USHORT)cap, "multi-byte");
        }
        printf("  2. two-, three- and four-byte sequences at every capacity, both directions: %ld\n",
               cases - before);
    }

    /* 3. invalid input */
    {
        long before = cases;
        static const wchar_t LONE[3] = { L'a', 0xD800, L'b' };
        static const char BAD[6] = { 'a', (char)0xFF, (char)0x80, (char)0xE2, (char)0x82, 'b' };
        for (cap = 0; cap <= 24; ++cap) {
            one8(LONE, 3, (USHORT)cap, "a lone surrogate");
            oneW(BAD, 6, (USHORT)cap, "malformed UTF-8");
        }
        printf("  3. a lone surrogate going out and malformed UTF-8 coming in, at every capacity --\n"
               "     both are REPLACED and the call still succeeds: %ld\n", cases - before);
    }

    /* 4. the allocating path */
    {
        long before = cases, badalloc = 0;
        for (n = 0; n <= 40; ++n) {
            USTR in;
            U8STR a, b;
            LONG ro, rl;
            for (i = 0; i < n; ++i) wsrc[i] = (wchar_t)(L'a' + (i % 26));
            in.Buffer = wsrc; in.Length = (USHORT)(n * 2); in.MaximumLength = in.Length;
            memset(&a, 0xCD, sizeof a);
            memset(&b, 0xCD, sizeof b);
            ro = wia_unicodestringtoutf8string(&a, &in, TRUE);
            rl = l_u2u8(&b, &in, TRUE);
            ++cases;
            if (ro != rl || a.Length != b.Length || a.MaximumLength != b.MaximumLength ||
                (ro == 0 && memcmp(a.Buffer, b.Buffer, a.Length + 1) != 0)) {
                ++badalloc; ++fails;
                if (fails <= 20)
                    printf("  MISMATCH [allocating] n=%d: ours %08lX/%u/%u  live %08lX/%u/%u\n",
                           n, (unsigned long)ro, a.Length, a.MaximumLength,
                           (unsigned long)rl, b.Length, b.MaximumLength);
            }
            /* The block ours allocated must be freeable by the paired export. If it were not, a
               caller doing the normal thing would corrupt its heap, and no comparison of Length
               would ever notice. */
            if (ro == 0 && l_freeu8) l_freeu8(&a);
            if (rl == 0 && l_freeu8) l_freeu8(&b);
        }
        printf("  4. the ALLOCATING path at every length, with each block freed by the paired\n"
               "     RtlFreeUTF8String: %ld (%ld bad)\n", cases - before, badalloc);
    }

    /* 5. randomised */
    {
        long before = cases;
        int trial;
        for (trial = 0; trial < 40000; ++trial) {
            int len = (int)(rnd() % 40);
            int c = (int)(rnd() % 60);
            int mode = (int)(rnd() % 3);
            for (i = 0; i < len; ++i) {
                unsigned r = rnd();
                wsrc[i] = (mode == 0) ? (wchar_t)(L'a' + (r % 26))
                        : (mode == 1) ? (wchar_t)(0x80 + (r % 0x700))
                                      : (wchar_t)r;
                asrc[i] = (mode == 0) ? (char)('a' + (r % 26)) : (char)r;
            }
            one8(wsrc, len, (USHORT)c, "randomised");
            oneW(asrc, len, (USHORT)c, "randomised");
        }
        printf("  5. randomised lengths, contents and capacities, both directions: %ld\n",
               cases - before);
    }

    /* 6. the allocating path in both directions, with content that produces every status */
    {
        long before = cases;
        static const wchar_t MB[4] = { 0x00E9, 0x20AC, 0xD83D, 0xDE00 };
        static const wchar_t LONE[3] = { L'a', 0xD800, L'b' };
        static const char U8MB[9] = { (char)0xC3,(char)0xA9, (char)0xE2,(char)0x82,(char)0xAC,
                                      (char)0xF0,(char)0x9F,(char)0x98,(char)0x80 };
        static const char BAD[6] = { 'a', (char)0xFF, (char)0x80, (char)0xE2, (char)0x82, 'b' };
        for (n = 0; n <= 40; ++n) {
            for (i = 0; i < n; ++i) { wsrc[i] = (wchar_t)(L'a' + (i % 26)); asrc[i] = (char)('a' + (i % 26)); }
            alloc8(wsrc, n, "ASCII");
            allocW(asrc, n, "ASCII");
        }
        for (n = 1; n <= 4; ++n) alloc8(MB, n, "multi-byte");
        for (n = 1; n <= 9; ++n) allocW(U8MB, n, "multi-byte");
        for (n = 1; n <= 3; ++n) alloc8(LONE, n, "a lone surrogate");
        for (n = 1; n <= 6; ++n) allocW(BAD, n, "malformed UTF-8");
        /* the randomised allocating path, which is where a wrong Length shows up as a heap
           corruption on the free rather than as a comparison failure */
        {
            int trial;
            for (trial = 0; trial < 4000; ++trial) {
                int len = (int)(rnd() % 40);
                int mode = (int)(rnd() % 3);
                for (i = 0; i < len; ++i) {
                    unsigned r = rnd();
                    wsrc[i] = (mode == 0) ? (wchar_t)(L'a' + (r % 26))
                            : (mode == 1) ? (wchar_t)(0x80 + (r % 0x700))
                                          : (wchar_t)r;
                    asrc[i] = (mode == 0) ? (char)('a' + (r % 26)) : (char)r;
                }
                alloc8(wsrc, len, "randomised");
                allocW(asrc, len, "randomised");
            }
        }
        printf("  6. the ALLOCATING path in BOTH directions -- ASCII, multi-byte, a lone surrogate\n"
               "     and malformed UTF-8, every block freed by its paired export: %ld\n",
               cases - before);
    }

    /* 7. the USHORT boundary, which nothing above can reach */
    {
        long before = cases;
        int want;
        for (want = 65528; want <= 65540; ++want) big8(want, 0xFFFF, "at the field limit");
        big8(90000, 0xFFFF, "far past the field limit");
        /* and with a destination that is ALSO too small, to pin which check wins */
        big8(90000, 4, "too big AND too small");
        big8(90000, 0, "too big AND zero capacity");
        big8(65532, 4, "legal size, too small");

        for (n = 32760; n <= 32770; ++n) bigW(n, 0xFFFF, "at the field limit");
        bigW(40000, 0xFFFF, "far past the field limit");
        bigW(40000, 4, "too big AND too small");
        bigW(40000, 0, "too big AND zero capacity");
        bigW(32000, 4, "legal size, too small");
        printf("  7. the USHORT boundary in both directions -- STATUS_INVALID_PARAMETER_2, and\n"
               "     which check wins when the result is too big AND the destination too small: %ld\n",
               cases - before);
    }

    /* 8. long but legal sources, which take the sizing fallback in both directions */
    {
        long before = cases;
        for (n = 20000; n <= 32000; n += 4000) {
            big8(n, 0xFFFF, "a long legal source");
            big8(n, (n / 2) | 1, "a long source, tight destination");
            bigW(n / 2, 0xFFFF, "a long legal source");
            bigW(n / 2, (n / 2) | 1, "a long source, tight destination");
        }
        printf("  8. long but legal sources at a generous and at a tight capacity -- the paths that\n"
               "     fall back to sizing the output first: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  the live exports returned SUCCESS %ld, BUFFER_TOO_SMALL %ld, BUFFER_OVERFLOW %ld,\n"
           "  SOME_NOT_MAPPED %ld and INVALID_PARAMETER_2 %ld -- all five outcomes matter, the two\n"
           "  failure codes are NOT interchangeable between the two directions, and the last one\n"
           "  is not a shortfall at all\n",
           n_ok, n_toosmall, n_overflow, n_notmapped, n_toobig);
    if (!n_ok || !n_toosmall || !n_overflow || !n_notmapped || !n_toobig) {
        printf("CORRECTNESS: FAILED (an outcome was never produced)\n");
        return 1;
    }
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (status, both lengths and the whole buffer exact vs live)\n");
    return fails ? 1 : 0;
}
