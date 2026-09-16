/* changes/269-convertstringsidtosid/probes/model.c
 *
 * THE MODEL IS VALIDATED BEFORE A LINE OF ASSEMBLY IS WRITTEN.
 *
 * reference.c states the grammar the three probes measured. This file checks that statement against
 * the live export over a corpus built to reach every clause of it -- because a model that is merely
 * *consistent with* the probe table is not the same as a model that agrees with the export, and the
 * difference only shows up on inputs nobody hand-wrote.
 *
 * Compared on every case: the BOOL, `GetLastError()` on failure, the output pointer's treatment on
 * failure, and on success the SID's whole byte image and its length.
 *
 * The corpus is generated rather than listed, from the parts the grammar has: a prefix, a revision,
 * an authority, a count, and a per-field NUMBER FORM -- decimal, hex, leading zeros, over-long,
 * empty, signed, and with a trailing letter. Enumerating the cross product is what reaches the
 * combinations a hand-written list does not: a hex authority with a saturating sub-authority and a
 * trailing separator is not a case anyone writes down.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

BOOL ref_str2sid(const wchar_t*, PSID*);
int  wia_sid_alias_init(void);
int  wia_sid_classify_init(void);

static long cases, bad;
static long n_ok, n_sid, n_over, n_param;

static void one(const wchar_t* s)
{
    PSID a = (PSID)(UINT_PTR)0xABCDEF01, b = (PSID)(UINT_PTR)0xABCDEF01;
    BOOL ra, rb;
    DWORD ea, eb;
    ++cases;
    SetLastError(0); ra = ConvertStringSidToSidW(s, &a); ea = GetLastError();
    SetLastError(0); rb = ref_str2sid(s, &b);            eb = GetLastError();
    if (ra) { if (ea == 0) ++n_ok; }
    else if (ea == ERROR_INVALID_SID) ++n_sid;
    else if (ea == ERROR_ARITHMETIC_OVERFLOW) ++n_over;
    else if (ea == ERROR_INVALID_PARAMETER) ++n_param;

    if ((ra != 0) != (rb != 0) || (!ra && ea != eb) ||
        (!ra && (a == (PSID)(UINT_PTR)0xABCDEF01) != (b == (PSID)(UINT_PTR)0xABCDEF01))) {
        if (++bad <= 12)
            printf("  MISMATCH %-46ls live %s/%lu  model %s/%lu%s\n", s,
                   ra ? "OK " : "NO ", (unsigned long)ea, rb ? "OK " : "NO ", (unsigned long)eb,
                   (!ra && (a == (PSID)(UINT_PTR)0xABCDEF01) != (b == (PSID)(UINT_PTR)0xABCDEF01))
                       ? "  (the output pointer was treated differently)" : "");
    } else if (ra && rb) {
        DWORD la = GetLengthSid(a), lb = GetLengthSid(b);
        if (la != lb || memcmp(a, b, la) != 0) {
            if (++bad <= 12) {
                DWORD i;
                printf("  MISMATCH %-46ls bytes differ (len %lu / %lu):", s,
                       (unsigned long)la, (unsigned long)lb);
                for (i = 0; i < la && i < 16; ++i)
                    printf(" %02X/%02X", ((unsigned char*)a)[i], ((unsigned char*)b)[i]);
                printf("\n");
            }
        }
    }
    if (ra && a != (PSID)(UINT_PTR)0xABCDEF01) LocalFree(a);
    if (rb && b != (PSID)(UINT_PTR)0xABCDEF01) LocalFree(b);
}

/* the number forms the grammar distinguishes */
static const wchar_t* NUM[] = {
    L"0", L"1", L"5", L"18", L"012", L"000000000000000000018",
    L"0x0", L"0x12", L"0xAB", L"0xab", L"0X12", L"0x",
    L"4294967295", L"4294967296", L"18446744073709551616", L"0xFFFFFFFF", L"0xFFFFFFFFF",
    L"281474976710655", L"281474976710656", L"0xFFFFFFFFFFFF", L"0x1000000000000",
    L"255", L"256", L"", L"+1", L"-1", L"1a", L"1 8", L"x", L" 1"
};
#define NNUM (int)(sizeof NUM / sizeof NUM[0])

int main(void)
{
    static wchar_t s[4096];
    int i, j, k, m;
    setvbuf(stdout, NULL, _IONBF, 0);
    { int rc = wia_sid_classify_init();
      if (rc) { printf("the character classes failed to build: code %d\n", rc); return 1; }
      rc = wia_sid_alias_init();
      if (rc) { printf("the alias table failed to build: code %d\n", rc); return 1; } }
    printf("== the model against the live export ==\n");

    /* 1. every number form in every field position */
    for (i = 0; i < NNUM; ++i) {
        for (j = 0; j < NNUM; ++j) {
            for (k = 0; k < NNUM; ++k) {
                wsprintfW(s, L"S-%s-%s-%s", NUM[i], NUM[j], NUM[k]);
                one(s);
            }
        }
    }
    printf("  every number form in each of the three fields: %ld\n", cases);

    /* 2. the prefix, the separators, and the shape */
    {
        long before = cases;
        static const wchar_t* SH[] = {
            L"S-1-5-18", L"s-1-5-18", L"z-1-5-18", L"SS-1-5-18", L"S1-5-18", L"S-1-5-18-",
            L"S-1-5--18", L"S--1-5-18", L"S-1--5-18", L"-S-1-5-18", L"S-1-5-18 ", L" S-1-5-18",
            L"S", L"S-", L"S-1", L"S-1-", L"S-1-5", L"S-1-5-", L"", L"-", L"--",
            L"S-1-5-18-19", L"S-1-0-0", L"S-1-5-0", L"S-0-5-18", L"S-255-5-18", L"S-256-5-18",
            L"S-1-5-18\t", L"S-1-5-1\n8", L"S\0-1-5-18"
        };
        for (i = 0; i < (int)(sizeof SH / sizeof SH[0]); ++i) one(SH[i]);
        printf("  prefixes, separators and shapes: %ld\n", cases - before);
    }

    /* 3. every sub-authority count from 0 to 260, which straddles the 254 limit */
    {
        long before = cases;
        for (k = 0; k <= 260; ++k) {
            int n = 0;
            n += wsprintfW(s + n, L"S-1-5");
            for (m = 0; m < k; ++m) n += wsprintfW(s + n, L"-%d", (m % 9) + 1);
            one(s);
        }
        printf("  every sub-authority count 0..260 (the limit is 254): %ld\n", cases - before);
    }

    /* 4. every two-character string over printable ASCII: the alias table, exhaustively */
    {
        long before = cases;
        int a, b;
        for (a = 0x20; a < 0x7F; ++a)
            for (b = 0x20; b < 0x7F; ++b) {
                s[0] = (wchar_t)a; s[1] = (wchar_t)b; s[2] = 0;
                one(s);
            }
        printf("  every two-character string over printable ASCII: %ld\n", cases - before);
    }

    /* 5. one-character and three-character strings, which must NOT be aliases */
    {
        long before = cases;
        int a;
        for (a = 0x20; a < 0x7F; ++a) {
            s[0] = (wchar_t)a; s[1] = 0; one(s);
            s[0] = L'B'; s[1] = L'A'; s[2] = (wchar_t)a; s[3] = 0; one(s);
        }
        printf("  one- and three-character strings: %ld\n", cases - before);
    }

    /* 6. the NULL arguments */
    {
        PSID p = 0;
        BOOL ra, rb;
        DWORD ea, eb;
        ++cases;
        SetLastError(0); ra = ConvertStringSidToSidW(0, &p); ea = GetLastError();
        SetLastError(0); rb = ref_str2sid(0, &p);            eb = GetLastError();
        if (!ra && ea == ERROR_INVALID_PARAMETER) ++n_param;
        if ((ra != 0) != (rb != 0) || ea != eb) { ++bad; printf("  MISMATCH NULL string\n"); }
        ++cases;
        SetLastError(0); ra = ConvertStringSidToSidW(L"S-1-5-18", 0); ea = GetLastError();
        SetLastError(0); rb = ref_str2sid(L"S-1-5-18", 0);            eb = GetLastError();
        if (!ra && ea == ERROR_INVALID_PARAMETER) ++n_param;
        if ((ra != 0) != (rb != 0) || ea != eb) { ++bad; printf("  MISMATCH NULL out\n"); }
        printf("  the NULL arguments: 2\n");
    }

    printf("\n  %ld cases, %ld mismatches\n", cases, bad);
    printf("  the live export answered OK %ld, INVALID_SID %ld, ARITHMETIC_OVERFLOW %ld,\n"
           "  INVALID_PARAMETER %ld -- all four outcomes are reached\n",
           n_ok, n_sid, n_over, n_param);
    if (!n_ok || !n_sid || !n_over || !n_param) {
        printf("MODEL: FAILED (an outcome was never produced)\n");
        return 1;
    }
    printf(bad ? "MODEL: FAILED\n" : "MODEL: PASS (the grammar in reference.c is the export's)\n");
    return bad ? 1 : 0;
}
