/* changes/269-convertstringsidtosid/correctness.c
 *
 * THREE-WAY: ours vs the scalar model vs the LIVE advapi32 export.
 *
 * The model was validated on its own first -- probes/model.c, 36508 cases, zero disagreements --
 * because a grammar this surprising had to be pinned before any assembly could be written against
 * it. This gate runs the same corpora with the assembly in the middle, and adds the cases that only
 * matter once there IS an implementation: the whole byte image of the SID, the length, the output
 * pointer on failure, and every code unit in every field position.
 *
 * What is compared on every case: the BOOL, `GetLastError()` on failure, whether the output pointer
 * was touched, and on success the SID's length and every byte of it.
 *
 * THE CORPORA:
 *   1. every number form in each of the three field positions -- the cross product, 27000 cases,
 *      which is what reaches combinations nobody writes down by hand;
 *   2. prefixes, separators and shapes;
 *   3. every sub-authority count from 0 to 260, straddling the 254 limit and its
 *      ERROR_ARITHMETIC_OVERFLOW;
 *   4. Every code unit 1..0xFFFF in each of the three field positions, twice each -- leading, where
 *      whitespace is legal in two of the three, and trailing, where it is legal in none. This is
 *      what covers the Unicode digit and whitespace sets without a table of them appearing here;
 *   5. every two-character string over printable ASCII: the alias table, exhaustively;
 *   6. the hexadecimal-carry cases, which are the ones a decimal-only corpus never reaches;
 *   7. the NULL arguments.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

BOOL wia_str2sid(const wchar_t*, PSID*);
BOOL ref_str2sid(const wchar_t*, PSID*);
int  wia_sid_alias_init(void);
int  wia_sid_classify_init(void);

static long cases, bad;
static long n_ok, n_sid, n_over, n_param;

#define POISON ((PSID)(UINT_PTR)0xABCDEF01)

/* The sentinel is not zero, and that matters. The first version of this gate wrote
 *
 *     SetLastError(0); rb = wia_str2sid(s, &b); eb = GetLastError();
 *
 * which cannot tell "the export left the last error alone" from "the export set it to zero" --
 * they read identically from a pre-value of zero. So 429776 cases agreed with the live export
 * while the implementation disagreed with it on every successful call a real program makes: all
 * four exports of the SID text family ZERO the last error on success (changes/272-.../probes/
 * lasterror.c asks each of them from six starting values) and this one did not. It was change
 * 272's gate that found it, because its corpus happened to use a non-zero sentinel.
 *
 * It is the same defect as change 067's corpus stepping MaximumLength by two: not a weak test, an
 * absent one -- the generator could not express the case. */
#define SENTINEL 0x0D15EA5Eul
static void one(const wchar_t* s)
{
    PSID a = POISON, b = POISON, c = POISON;
    BOOL ra, rb, rc;
    DWORD ea, eb, ec;
    ++cases;
    SetLastError(SENTINEL); ra = ConvertStringSidToSidW(s, &a); ea = GetLastError();
    SetLastError(SENTINEL); rb = wia_str2sid(s, &b);            eb = GetLastError();
    SetLastError(SENTINEL); rc = ref_str2sid(s, &c);            ec = GetLastError();

    if (ra) ++n_ok;
    else if (ea == ERROR_INVALID_SID) ++n_sid;
    else if (ea == ERROR_ARITHMETIC_OVERFLOW) ++n_over;
    else if (ea == ERROR_INVALID_PARAMETER) ++n_param;

    /* The last error is compared on every call, success included. It used to be compared only when
       the call FAILED -- `(!ra && (ea != eb ...))` -- which is the other half of the blindness the
       sentinel note above describes: even with a non-zero pre-value, a success-path difference was
       simply not looked at. Both halves had to be wrong for the defect to survive, and both were. */
    if ((ra != 0) != (rb != 0) || (ra != 0) != (rc != 0) ||
        ea != eb || ea != ec ||
        ((a == POISON) != (b == POISON) || (a == POISON) != (c == POISON))) {
        if (++bad <= 12)
            printf("  MISMATCH %-44ls live %s/%-5lu ours %s/%-5lu ref %s/%-5lu  pointer %s/%s/%s\n",
                   s, ra ? "OK" : "NO", (unsigned long)ea, rb ? "OK" : "NO", (unsigned long)eb,
                   rc ? "OK" : "NO", (unsigned long)ec,
                   a == POISON ? "kept" : "written", b == POISON ? "kept" : "written",
                   c == POISON ? "kept" : "written");
    } else if (ra && rb && rc) {
        DWORD la = GetLengthSid(a), lb = GetLengthSid(b), lc = GetLengthSid(c);
        if (la != lb || la != lc || memcmp(a, b, la) != 0 || memcmp(a, c, la) != 0) {
            if (++bad <= 12) {
                DWORD i;
                printf("  MISMATCH %-44ls bytes (len %lu/%lu/%lu):", s,
                       (unsigned long)la, (unsigned long)lb, (unsigned long)lc);
                for (i = 0; i < la && i < 12; ++i)
                    printf(" %02X/%02X/%02X", ((unsigned char*)a)[i],
                           ((unsigned char*)b)[i], ((unsigned char*)c)[i]);
                printf("\n");
            }
        }
    }
    if (ra && a != POISON) LocalFree(a);
    if (rb && b != POISON) LocalFree(b);
    if (rc && c != POISON) LocalFree(c);
}

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
    if (wia_sid_classify_init()) { printf("the character classes failed to build\n"); return 1; }
    if (wia_sid_alias_init())    { printf("the alias table failed to build\n"); return 1; }
    printf("== CORRECTNESS: ConvertStringSidToSidW ==\n");

    for (i = 0; i < NNUM; ++i)
        for (j = 0; j < NNUM; ++j)
            for (k = 0; k < NNUM; ++k) {
                wsprintfW(s, L"S-%s-%s-%s", NUM[i], NUM[j], NUM[k]);
                one(s);
            }
    printf("  1. every number form in each of the three fields: %ld\n", cases);

    {
        long before = cases;
        static const wchar_t* SH[] = {
            L"S-1-5-18", L"s-1-5-18", L"z-1-5-18", L"SS-1-5-18", L"S1-5-18", L"S-1-5-18-",
            L"S-1-5--18", L"S--1-5-18", L"S-1--5-18", L"-S-1-5-18", L"S-1-5-18 ", L" S-1-5-18",
            L"S", L"S-", L"S-1", L"S-1-", L"S-1-5", L"S-1-5-", L"", L"-", L"--",
            L"S-1-5-18-19", L"S-1-0-0", L"S-1-5-0", L"S-0-5-18", L"S-255-5-18", L"S-256-5-18",
            L"S-1-5-18\t", L"S-1-5-1\n8", L"S-1- 5-1", L"S-1-+5-1", L"S-1- +5-1", L"S-1-+ 5-1",
            L"S-1-++5-1", L"S-1-5 -1", L"S-+1-5-1", L"S-\t1-5-1", L"S-+0x1-5-1", L"S-1-+0x5-1",
            L"S-1-5-+1", L"S-1-5- 1", L"S-1-5-1-+2", L"S-1-5-1- 2"
        };
        for (i = 0; i < (int)(sizeof SH / sizeof SH[0]); ++i) one(SH[i]);
        printf("  2. prefixes, separators, signs and whitespace: %ld\n", cases - before);
    }

    {
        long before = cases;
        for (k = 0; k <= 260; ++k) {
            int n = 0;
            n += wsprintfW(s + n, L"S-1-5");
            for (m = 0; m < k; ++m) n += wsprintfW(s + n, L"-%d", (m % 9) + 1);
            one(s);
        }
        printf("  3. every sub-authority count 0..260 (the limit is 254): %ld\n", cases - before);
    }

    {
        long before = cases;
        int c;
        for (c = 1; c < 0x10000; ++c) {
            /* leading in each of the three fields */
            s[0] = L'S'; s[1] = L'-'; s[2] = (wchar_t)c; s[3] = L'1'; s[4] = L'-';
            s[5] = L'5'; s[6] = L'-'; s[7] = L'1'; s[8] = 0; one(s);
            s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = L'-'; s[4] = (wchar_t)c;
            s[5] = L'5'; s[6] = L'-'; s[7] = L'1'; s[8] = 0; one(s);
            s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = L'-'; s[4] = L'5'; s[5] = L'-';
            s[6] = (wchar_t)c; s[7] = L'1'; s[8] = 0; one(s);
            /* and trailing, where whitespace is legal in none of them */
            s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = (wchar_t)c; s[4] = L'-';
            s[5] = L'5'; s[6] = L'-'; s[7] = L'1'; s[8] = 0; one(s);
            s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = L'-'; s[4] = L'5'; s[5] = (wchar_t)c;
            s[6] = L'-'; s[7] = L'1'; s[8] = 0; one(s);
            s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = L'-'; s[4] = L'5'; s[5] = L'-';
            s[6] = L'1'; s[7] = (wchar_t)c; s[8] = 0; one(s);
        }
        printf("  4. every code unit 1..0xFFFF, leading and trailing, in all three fields: %ld\n",
               cases - before);
    }

    {
        long before = cases;
        int a, b;
        for (a = 0x20; a < 0x7F; ++a)
            for (b = 0x20; b < 0x7F; ++b) {
                s[0] = (wchar_t)a; s[1] = (wchar_t)b; s[2] = 0;
                one(s);
            }
        for (a = 0x20; a < 0x7F; ++a) {
            s[0] = (wchar_t)a; s[1] = 0; one(s);
            s[0] = L'B'; s[1] = L'A'; s[2] = (wchar_t)a; s[3] = 0; one(s);
        }
        printf("  5. the alias table, exhaustively, plus one- and three-character strings: %ld\n",
               cases - before);
    }

    {
        long before = cases;
        static const wchar_t* HX[] = {
            L"S-0x1-5-18", L"S-0x1-5-1a", L"S-0x1-10-10", L"S-1-0x5-18", L"S-1-0x5-1a",
            L"S-1-0x10-10", L"S-1-5-0x10-10", L"S-1-5-10-0x10", L"S-1-5-0x1a-1a",
            L"S-1-5-10-1a", L"S-0x1-0x5-18", L"S-0x1-5-0x18", L"S-1-5-0x10-0x10",
            L"S-0x1-5-FFFFFFFF", L"S-0x1-5-FFFFFFFFF", L"S-0x1-FFFFFFFFFFFF-1",
            L"S-0x1-1000000000000-1", L"S-0x1-0x1000000000000-1", L"S-0x0-0-18", L"S-0x0-0-1a",
            L"S-0X1-5-18", L"S-0x-5-18", L"S-0x1-5-0x", L"S-0x1-5- 1", L"S-0x1-5-+1"
        };
        for (i = 0; i < (int)(sizeof HX / sizeof HX[0]); ++i) one(HX[i]);
        /* and the carry with every count, since the base is set once and used many times */
        for (k = 1; k <= 20; ++k) {
            int n = 0;
            n += wsprintfW(s + n, L"S-0x1-5");
            for (m = 0; m < k; ++m) n += wsprintfW(s + n, L"-%x", (m * 7 + 10) & 0xFF);
            one(s);
        }
        printf("  6. the hexadecimal carry, which a decimal corpus never reaches: %ld\n",
               cases - before);
    }

    {
        PSID p = POISON;
        BOOL ra, rb, rc;
        DWORD ea, eb, ec;
        ++cases;
        SetLastError(SENTINEL); ra = ConvertStringSidToSidW(0, &p); ea = GetLastError();
        SetLastError(SENTINEL); rb = wia_str2sid(0, &p);            eb = GetLastError();
        SetLastError(SENTINEL); rc = ref_str2sid(0, &p);            ec = GetLastError();
        if (!ra && ea == ERROR_INVALID_PARAMETER) ++n_param;
        if ((ra != 0) != (rb != 0) || (ra != 0) != (rc != 0) || ea != eb || ea != ec) {
            ++bad; printf("  MISMATCH NULL string: %lu/%lu/%lu\n",
                          (unsigned long)ea, (unsigned long)eb, (unsigned long)ec);
        }
        ++cases;
        SetLastError(SENTINEL); ra = ConvertStringSidToSidW(L"S-1-5-18", 0); ea = GetLastError();
        SetLastError(SENTINEL); rb = wia_str2sid(L"S-1-5-18", 0);            eb = GetLastError();
        SetLastError(SENTINEL); rc = ref_str2sid(L"S-1-5-18", 0);            ec = GetLastError();
        if (!ra && ea == ERROR_INVALID_PARAMETER) ++n_param;
        if ((ra != 0) != (rb != 0) || (ra != 0) != (rc != 0) || ea != eb || ea != ec) {
            ++bad; printf("  MISMATCH NULL out: %lu/%lu/%lu\n",
                          (unsigned long)ea, (unsigned long)eb, (unsigned long)ec);
        }
        printf("  7. the NULL arguments: 2\n");
    }

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, bad);
    printf("  the live export answered OK %ld, INVALID_SID %ld, ARITHMETIC_OVERFLOW %ld,\n"
           "  INVALID_PARAMETER %ld -- all four outcomes matter and the gate fails without them\n",
           n_ok, n_sid, n_over, n_param);
    if (!n_ok || !n_sid || !n_over || !n_param) {
        printf("CORRECTNESS: FAILED (an outcome was never produced)\n");
        return 1;
    }
    printf(bad ? "CORRECTNESS: FAILED\n"
               : "CORRECTNESS: PASS (status, last-error, the output pointer and every SID byte\n"
                 "exact vs live advapi32 and vs the scalar model)\n");
    return bad ? 1 : 0;
}
