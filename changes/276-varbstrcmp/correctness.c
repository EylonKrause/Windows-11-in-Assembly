/* changes/276-varbstrcmp/correctness.c
 *
 * Gate 1 for oleaut32!VarBstrCmp: Ours vs the scalar model vs the live export, on the HRESULT.
 *
 * The one thing this gate exists to catch is the fast path answering eq where the export would not.
 * That can happen three ways, and the corpus is built around all three:
 *
 *   1. The arguments are invalid. probes/errors.c found that a non-empty pair validates even when
 *      the two operands are the SAME POINTER, `VarBstrCmp(x, x, ..., 0x40)` is E_INVALIDARG, not
 *      EQ, while the empty cases do not validate at all. So every shape is asked under every
 *      single-bit flag value 0..31 and under invalid locales, and the identical pairs are asked
 *      most of all.
 *
 *   2. The strings are not really identical. The byte comparison runs 32 bytes at a time with an
 *      OVERLAPPING tail, so a difference in the last few characters, or in the characters the
 *      overlap covers twice, is exactly where it would be missed. Every length 16..200 is asked with
 *      the difference walked through every position.
 *
 *   3. The lengths differ. Two BSTRs of different length always collate, because "co-op" and "coop"
 *      are different lengths and compare GT, not by length.
 *
 * And the threshold itself. The fast path only runs at sixteen characters or more, so every length
 * from 0 to 40 is asked with equal content; the answer must be the same on both sides of the
 * boundary, which is the one thing a threshold can get wrong.
 *
 * Embedded NULs are in the corpus because a BSTR is counted, not terminated: probes/contract.c
 * measured "a\0bcd" vs "a\0xyz" as LT, so the comparison uses the whole counted length.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "oleaut32.lib")

extern long wia_varbstrcmp(BSTR, BSTR, unsigned long, unsigned long);
extern int  wia_vbc_init(void);
long ref_varbstrcmp(BSTR, BSTR, unsigned long, unsigned long);

static int  failures = 0;
static long cases = 0;
static long n_eq = 0, n_lt = 0, n_gt = 0, n_err = 0;

static void one(BSTR a, BSTR b, unsigned long lcid, unsigned long flags)
{
    long v = (long)VarBstrCmp(a, b, lcid, flags);
    long o = wia_varbstrcmp(a, b, lcid, flags);
    long m = ref_varbstrcmp(a, b, lcid, flags);
    ++cases;
    if (v == VARCMP_EQ) ++n_eq;
    else if (v == VARCMP_LT) ++n_lt;
    else if (v == VARCMP_GT) ++n_gt;
    else ++n_err;
    if (v != o || v != m) {
        if (failures < 12)
            printf("  FAIL len %u/%u lcid %08lX flags %08lX: live %08lX  ours %08lX  model %08lX\n",
                   a ? (unsigned)SysStringLen(a) : 0u, b ? (unsigned)SysStringLen(b) : 0u,
                   lcid, flags, (unsigned long)v, (unsigned long)o, (unsigned long)m);
        ++failures;
    }
}

int main(void)
{
    static wchar_t p[512], q[512];
    unsigned i, j, k;
    unsigned long seed = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_vbc_init()) { printf("the flag mask failed to build\n"); return 1; }
    printf("== CORRECTNESS: VarBstrCmp ==\n");

    /* 1. NULL and empty, under every single-bit flag and two invalid locales */
    {
        long before = cases;
        BSTR e = SysAllocString(L""), s = SysAllocString(L"abc");
        static const unsigned long LC[] = { LOCALE_USER_DEFAULT, LOCALE_INVARIANT, 0x0FFFFFFFul, 0 };
        for (i = 0; i < 4; ++i)
            for (j = 0; j < 33; ++j) {
                unsigned long fl = (j == 32) ? 0ul : (1ul << j);
                one(0, 0, LC[i], fl);
                one(0, e, LC[i], fl);
                one(e, 0, LC[i], fl);
                one(0, s, LC[i], fl);
                one(s, 0, LC[i], fl);
                one(e, e, LC[i], fl);
                one(e, s, LC[i], fl);
                one(s, e, LC[i], fl);
            }
        SysFreeString(e); SysFreeString(s);
        printf("  1. NULL and empty, 33 flag values x 4 locales: %ld\n", cases - before);
    }

    /* 2. IDENTICAL strings under every single-bit flag and invalid locales, the fast path's own
          question, since the export validates even when the operands are the same pointer */
    {
        long before = cases;
        static const unsigned long LC[] = { LOCALE_USER_DEFAULT, LOCALE_INVARIANT, 0x0FFFFFFFul };
        static const int LENS[] = { 1, 8, 15, 16, 17, 32, 64, 200 };
        for (k = 0; k < sizeof LENS / sizeof LENS[0]; ++k) {
            int n = LENS[k];
            BSTR a, b;
            for (i = 0; i < (unsigned)n; ++i) p[i] = (wchar_t)(L'a' + (i % 26));
            a = SysAllocStringLen(p, (UINT)n);
            b = SysAllocStringLen(p, (UINT)n);
            for (i = 0; i < 3; ++i)
                for (j = 0; j < 33; ++j) {
                    unsigned long fl = (j == 32) ? 0ul : (1ul << j);
                    one(a, b, LC[i], fl);       /* equal by content */
                    one(a, a, LC[i], fl);       /* the SAME pointer */
                }
            SysFreeString(a); SysFreeString(b);
        }
        printf("  2. identical pairs, 8 lengths x 33 flags x 3 locales, by content and by pointer: %ld\n",
               cases - before);
    }

    /* 3. every LENGTH 0..40 with equal content; the threshold is at 16 and both sides must agree */
    {
        long before = cases;
        for (k = 0; k <= 40; ++k) {
            BSTR a, b;
            for (i = 0; i < k; ++i) p[i] = (wchar_t)(L'a' + (i % 26));
            a = SysAllocStringLen(p, (UINT)k);
            b = SysAllocStringLen(p, (UINT)k);
            one(a, b, LOCALE_USER_DEFAULT, 0);
            one(a, b, LOCALE_USER_DEFAULT, NORM_IGNORECASE);
            one(a, a, LOCALE_USER_DEFAULT, 0);
            SysFreeString(a); SysFreeString(b);
        }
        printf("  3. every length 0..40 with equal content, across the threshold: %ld\n",
               cases - before);
    }

    /* 4. a difference walked through every position, at every length 16..200. The byte comparison
          is 32 bytes at a time with an overlapping tail, so the last characters and the ones the
          overlap covers twice are where a difference would be missed. */
    {
        long before = cases;
        for (k = 16; k <= 200; k += (k < 40 ? 1 : 13)) {
            for (i = 0; i < k; ++i) { p[i] = (wchar_t)(L'a' + (i % 26)); q[i] = p[i]; }
            for (j = 0; j < k; ++j) {
                BSTR a, b;
                q[j] = (wchar_t)(p[j] + 1);
                a = SysAllocStringLen(p, (UINT)k);
                b = SysAllocStringLen(q, (UINT)k);
                one(a, b, LOCALE_USER_DEFAULT, 0);
                SysFreeString(a); SysFreeString(b);
                q[j] = p[j];
            }
        }
        printf("  4. a difference at every position, lengths 16..200: %ld\n", cases - before);
    }

    /* 5. different lengths, which always collate */
    {
        long before = cases;
        static const wchar_t* PAIR[][2] = {
            { L"co-op", L"coop" }, { L"can't", L"cant" }, { L"a", L"ab" }, { L"ab", L"a" },
            { L"abc", L"abcd" }, { L"Stra\x00DFe", L"Strasse" }, { L"\x00E4", L"ae" }
        };
        for (k = 0; k < sizeof PAIR / sizeof PAIR[0]; ++k) {
            BSTR a = SysAllocString(PAIR[k][0]), b = SysAllocString(PAIR[k][1]);
            for (j = 0; j < 8; ++j) one(a, b, LOCALE_USER_DEFAULT, (j == 7) ? 0ul : (1ul << j));
            SysFreeString(a); SysFreeString(b);
        }
        printf("  5. pairs where length and collation disagree: %ld\n", cases - before);
    }

    /* 6. embedded NULs; a BSTR is counted, not terminated */
    {
        long before = cases;
        static const wchar_t A[6] = { 'a', 0, 'b', 'c', 0, 'd' };
        static const wchar_t B[6] = { 'a', 0, 'b', 'c', 0, 'e' };
        BSTR a = SysAllocStringLen(A, 6), b = SysAllocStringLen(B, 6);
        BSTR c = SysAllocStringLen(A, 6);
        one(a, b, LOCALE_USER_DEFAULT, 0);
        one(a, c, LOCALE_USER_DEFAULT, 0);
        one(a, a, LOCALE_USER_DEFAULT, 0);
        SysFreeString(a); SysFreeString(b); SysFreeString(c);
        printf("  6. embedded NULs: %ld\n", cases - before);
    }

    /* 7. randomised content and flags */
    {
        long before = cases;
        for (i = 0; i < 60000 && failures < 12; ++i) {
            int n, m;
            BSTR a, b;
            unsigned long fl, lc;
            seed = seed * 1103515245u + 12345u;
            n = (int)((seed >> 8) % 90);
            seed = seed * 1103515245u + 12345u;
            m = ((seed >> 8) & 3) ? n : (int)((seed >> 12) % 90);   /* usually the same length */
            for (j = 0; j < (unsigned)n; ++j) {
                seed = seed * 1103515245u + 12345u;
                p[j] = (wchar_t)(((seed >> 16) & 1) ? (L'a' + (seed % 26)) : (1 + (seed % 0xFFFF)));
            }
            for (j = 0; j < (unsigned)m; ++j) q[j] = (j < (unsigned)n) ? p[j] : (wchar_t)L'z';
            if (((seed >> 5) & 7) == 0 && m) {       /* one in eight gets a difference */
                seed = seed * 1103515245u + 12345u;
                q[(seed >> 7) % (unsigned)m] = (wchar_t)(1 + (seed % 0xFFFF));
            }
            seed = seed * 1103515245u + 12345u;
            fl = ((seed >> 9) & 7) ? 0ul : (1ul << ((seed >> 13) % 32));
            lc = ((seed >> 17) & 7) ? LOCALE_USER_DEFAULT : 0x0FFFFFFFul;
            a = SysAllocStringLen(p, (UINT)n);
            b = SysAllocStringLen(q, (UINT)m);
            one(a, b, lc, fl);
            SysFreeString(a); SysFreeString(b);
        }
        printf("  7. 60000 randomised, content, length, flags and locale: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    printf("  the live export answered EQ %ld, LT %ld, GT %ld and an ERROR %ld times\n",
           n_eq, n_lt, n_gt, n_err);
    if (n_eq < 1000 || n_lt < 100 || n_gt < 100 || n_err < 100) {
        printf("  the corpus did not reach every outcome -- the gate fails without them\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the HRESULT exact vs live oleaut32 and vs the scalar model, on\n"
               "both sides of the fast path's threshold and under every single-bit flag)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
