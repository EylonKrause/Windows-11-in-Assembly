/* changes/276-varbstrcmp/probes/reflexive.c
 *
 * The whole change rests on one claim: Identical strings compare equal. So measure it.
 *
 * probes/contract.c established that VarBstrCmp IS CompareStringW -- nine hand-picked pairs where a
 * linguistic comparison and an ordinal one disagree, and the export tracked the linguistic answer
 * every time. So the collation is the OS's and this project does not reimplement it (change 210's
 * notes say the same about linguistic comparison).
 *
 * What probes/contract.c ALSO found is where a change could live:
 *
 *     the SAME pointer twice        3145.00 ns     <-- comparing a BSTR with ITSELF
 *     equal, 4000 characters        3224.00 ns
 *     4000 vs 1 character             31.50 ns     CompareStringW exits early on its own
 *     differ at character 0           33.50 ns     ... at every length, 4 to 8000
 *
 * There is no identity check and no equal-content check. Two strings that are byte-for-byte the
 * same cost 0.8 ns per character to discover that, and the same POINTER twice costs it too.
 *
 * A fast path that answers EQ whenever the two operands are byte-identical would turn 3224 ns into
 * the cost of a memcmp -- but only if "byte-identical implies VARCMP_EQ" is TRUE, for every string,
 * under every flag combination and locale the caller might pass. That is reflexivity, and it is the
 * kind of thing everybody assumes and nobody checks. Linguistic collation has non-obvious corners:
 * ignorable characters, unpaired surrogates, noncharacters, and the flags that change what counts as
 * a difference. If CompareStringW is non-reflexive for even one of them, the fast path is a wrong
 * answer and this change does not exist.
 *
 * So it is swept:
 *
 *   1. every code unit 1..0xFFFF, alone, against itself;
 *   2. every code unit as part of a longer string, against itself;
 *   3. surrogate pairs, unpaired surrogates and noncharacters;
 *   4. under every documented flag and several locales;
 *   5. and the empty and NULL forms, which the fast path also has to get right.
 *
 * The second half of this file is the flags, because the fallback has to reproduce them. VarBstrCmp
 * takes a ULONG of flags and passes some subset to CompareStringW; which bits, and what it does with
 * the rest, decides what the fallback can call.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>

#pragma comment(lib, "oleaut32.lib")

static const char* vc(HRESULT h)
{
    switch (h) {
    case VARCMP_LT: return "LT";
    case VARCMP_EQ: return "EQ";
    case VARCMP_GT: return "GT";
    case VARCMP_NULL: return "NULL";
    default: return "?";
    }
}

int main(void)
{
    static wchar_t buf[64];
    int c, bad, i;

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== 1. every code unit 1..0xFFFF, alone, compared with itself ==\n");
    {
        bad = 0;
        for (c = 1; c < 0x10000; ++c) {
            BSTR x, y;
            buf[0] = (wchar_t)c; buf[1] = 0;
            x = SysAllocStringLen(buf, 1);
            y = SysAllocStringLen(buf, 1);
            if (VarBstrCmp(x, y, LOCALE_USER_DEFAULT, 0) != VARCMP_EQ) {
                if (bad < 12) printf("   U+%04X compares %s with itself\n", c,
                                     vc(VarBstrCmp(x, y, LOCALE_USER_DEFAULT, 0)));
                ++bad;
            }
            SysFreeString(x); SysFreeString(y);
        }
        printf("   %d of 65535 code units are NOT equal to themselves\n", bad);
    }

    printf("\n== 2. every code unit inside a longer string ==\n");
    {
        bad = 0;
        for (c = 1; c < 0x10000; ++c) {
            BSTR x, y;
            for (i = 0; i < 12; ++i) buf[i] = (wchar_t)(L'a' + i);
            buf[5] = (wchar_t)c;
            x = SysAllocStringLen(buf, 12);
            y = SysAllocStringLen(buf, 12);
            if (VarBstrCmp(x, y, LOCALE_USER_DEFAULT, 0) != VARCMP_EQ) {
                if (bad < 12) printf("   U+%04X inside a string compares not-equal to itself\n", c);
                ++bad;
            }
            SysFreeString(x); SysFreeString(y);
        }
        printf("   %d of 65535 are NOT equal to themselves inside a longer string\n", bad);
    }

    printf("\n== 3. surrogates, noncharacters and the awkward ones ==\n");
    {
        static const wchar_t* ODD[] = {
            L"\xD83D\xDE00",              /* a valid surrogate pair */
            L"\xD800",                    /* an unpaired high surrogate */
            L"\xDC00",                    /* an unpaired low surrogate */
            L"\xDC00\xD800",              /* a reversed pair */
            L"\xFFFE",                    /* a noncharacter */
            L"\xFFFF",
            L"a\x0301",                   /* a combining acute */
            L"\x00AD",                    /* a soft hyphen -- ignorable in collation */
            L"a\x00ADb",
            L"\x200B",                    /* a zero-width space */
            L"\x2060",                    /* a word joiner */
            L"  ",
            L"\x0000\x0000"               /* two NULs, via SysAllocStringLen below */
        };
        unsigned k;
        bad = 0;
        for (k = 0; k < sizeof ODD / sizeof ODD[0] - 1; ++k) {
            BSTR x = SysAllocString(ODD[k]), y = SysAllocString(ODD[k]);
            HRESULT r = VarBstrCmp(x, y, LOCALE_USER_DEFAULT, 0);
            printf("   %-22s -> %s%s\n", "a string of its own", vc(r),
                   r == VARCMP_EQ ? "" : "   <<< NOT REFLEXIVE");
            if (r != VARCMP_EQ) ++bad;
            SysFreeString(x); SysFreeString(y);
        }
        {
            static const wchar_t two[2] = { 0, 0 };
            BSTR x = SysAllocStringLen(two, 2), y = SysAllocStringLen(two, 2);
            HRESULT r = VarBstrCmp(x, y, LOCALE_USER_DEFAULT, 0);
            printf("   two embedded NULs      -> %s\n", vc(r));
            if (r != VARCMP_EQ) ++bad;
            SysFreeString(x); SysFreeString(y);
        }
        printf("   %d not reflexive\n", bad);
    }

    printf("\n== 4. under every documented flag, and several locales ==\n");
    {
        static const DWORD FLAGS[] = {
            0, NORM_IGNORECASE, NORM_IGNORENONSPACE, NORM_IGNORESYMBOLS,
            NORM_IGNOREWIDTH, NORM_IGNOREKANATYPE, SORT_STRINGSORT,
            NORM_IGNORECASE | NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS,
            0x7FFFFFFFul
        };
        static const LCID LCIDS[] = {
            LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT, LOCALE_INVARIANT,
            MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT),
            MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT), SORT_DEFAULT),
            MAKELCID(MAKELANGID(LANG_GERMAN, SUBLANG_DEFAULT), SORT_DEFAULT),
            MAKELCID(MAKELANGID(LANG_TURKISH, SUBLANG_DEFAULT), SORT_DEFAULT),
            0, 0x0FFFFFFFul
        };
        static const wchar_t* S[] = {
            L"abc", L"ABC", L"Stra\x00DFe", L"\x00E4\x00F6\x00FC", L"co-op", L"", L"i", L"I",
            L"\x30A2\x30A4", L"\xFF21\xFF22", L"a\x00ADb", L"\xD83D\xDE00"
        };
        unsigned fi, li, si;
        bad = 0;
        for (fi = 0; fi < sizeof FLAGS / sizeof FLAGS[0]; ++fi)
            for (li = 0; li < sizeof LCIDS / sizeof LCIDS[0]; ++li)
                for (si = 0; si < sizeof S / sizeof S[0]; ++si) {
                    BSTR x = SysAllocString(S[si]), y = SysAllocString(S[si]);
                    HRESULT r = VarBstrCmp(x, y, LCIDS[li], FLAGS[fi]);
                    if (r != VARCMP_EQ) {
                        if (bad < 12)
                            printf("   flags %08lX lcid %08lX \"%ls\" -> %s\n",
                                   (unsigned long)FLAGS[fi], (unsigned long)LCIDS[li], S[si], vc(r));
                        ++bad;
                    }
                    SysFreeString(x); SysFreeString(y);
                }
        printf("   %d of %d flag/locale/string combinations are NOT reflexive\n",
               bad, (int)(sizeof FLAGS / sizeof FLAGS[0] * sizeof LCIDS / sizeof LCIDS[0] *
                          sizeof S / sizeof S[0]));
    }

    printf("\n== 5. WHICH FLAGS DOES IT FORWARD? VarBstrCmp against CompareStringW, bit by bit ==\n");
    {
        int bit;
        printf("   bit  flag       \"a\" vs \"A\"      \"a b\" vs \"ab\"    result tracks CompareStringW?\n");
        for (bit = 0; bit < 32; ++bit) {
            DWORD fl = 1ul << bit;
            BSTR a1 = SysAllocString(L"a"), a2 = SysAllocString(L"A");
            BSTR b1 = SysAllocString(L"a b"), b2 = SysAllocString(L"ab");
            HRESULT v1 = VarBstrCmp(a1, a2, LOCALE_USER_DEFAULT, fl);
            HRESULT v2 = VarBstrCmp(b1, b2, LOCALE_USER_DEFAULT, fl);
            int c1 = CompareStringW(LOCALE_USER_DEFAULT, fl, L"a", 1, L"A", 1);
            int c2 = CompareStringW(LOCALE_USER_DEFAULT, fl, L"a b", 3, L"ab", 2);
            int m1 = (c1 == 0) ? -1 : (c1 - 1);
            int m2 = (c2 == 0) ? -1 : (c2 - 1);
            if (v1 != (HRESULT)m1 || v2 != (HRESULT)m2 || fl == 1 || fl == 0x1000)
                printf("   %2d   %08lX   %-4s (W %-4s)   %-4s (W %-4s)   %s\n",
                       bit, (unsigned long)fl, vc(v1), c1 ? vc((HRESULT)m1) : "err",
                       vc(v2), c2 ? vc((HRESULT)m2) : "err",
                       (v1 == (HRESULT)m1 && v2 == (HRESULT)m2) ? "yes" : "NO");
            SysFreeString(a1); SysFreeString(a2); SysFreeString(b1); SysFreeString(b2);
        }
        printf("   (only the bits that DIFFER from CompareStringW are printed, plus two controls)\n");
    }
    return 0;
}
