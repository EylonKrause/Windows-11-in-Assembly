/* changes/276-varbstrcmp/probes/errors.c
 *
 * The two things a fast path must not swallow.
 *
 * probes/reflexive.c proved that identical strings always compare EQ, 0 of 131070 code unit
 * placements, every surrogate and noncharacter asked, under every VALID flag and locale. The
 * qualifier is the point: its section 4 reported 176 of 972 combinations "not reflexive", and every
 * one of them had an invalid LCID (0x0FFFFFFF) or an undefined flag bit. Those are not non-reflexive
 * comparisons; they are error returns, and a fast path that answered eq before looking at the
 * arguments would turn an error into a success.
 *
 * probes/gap.c then established the budget. The shipped wrapper adds only 1.25-2.25 ns on top of the
 * CompareStringW it calls, so there is nothing to win on any comparison that has to be collated --
 * and 0.8 ns per character to win on every comparison that does not, because two equal 4000-character
 * strings cost 3208 ns and a memcmp of them costs 150.
 *
 * So the shape is forced: answer EQ without collating when the operands are byte-identical AND long
 * enough for the check to pay, delegate otherwise, and reproduce the error returns exactly. This
 * file measures the two things that decides:
 *
 *   1. What does it return for an invalid LCID or flag, exactly, the HRESULT, not "an error" --
 *      and does CompareStringW's failure map onto it?
 *   2. Does the empty case validate at all? If VarBstrCmp("", "") returns eq even with rubbish
 *      flags, then the empty rules are checked before the arguments are, and a fast path may do the
 *      same. If it returns the error, they are not.
 *
 * And one more, because it decides where the threshold goes: does an equal comparison cost the same
 * whether the strings are equal by content or the SAME POINTER? probes/gap.c says 3208 ns either
 * way, so the export does not even compare the pointers, but a length threshold has to be chosen
 * against the measured cost of the memcmp, not a guess.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>

#pragma comment(lib, "oleaut32.lib")

static void show(const char* what, HRESULT h)
{
    const char* n = "?";
    if (h == VARCMP_LT) n = "VARCMP_LT";
    else if (h == VARCMP_EQ) n = "VARCMP_EQ";
    else if (h == VARCMP_GT) n = "VARCMP_GT";
    else if (h == VARCMP_NULL) n = "VARCMP_NULL";
    else if (h == DISP_E_BADVARTYPE) n = "DISP_E_BADVARTYPE";
    else if (h == E_INVALIDARG) n = "E_INVALIDARG";
    else if (h == DISP_E_TYPEMISMATCH) n = "DISP_E_TYPEMISMATCH";
    printf("   %-46s -> %08lX  %s\n", what, (unsigned long)h, n);
}

int main(void)
{
    BSTR a, b, e;
    setvbuf(stdout, NULL, _IONBF, 0);
    a = SysAllocString(L"abc");
    b = SysAllocString(L"abd");
    e = SysAllocString(L"");

    printf("== 1. an invalid LCID ==\n");
    show("\"abc\" vs \"abd\", lcid 0x0FFFFFFF", VarBstrCmp(a, b, 0x0FFFFFFFul, 0));
    show("\"abc\" vs \"abc\", lcid 0x0FFFFFFF", VarBstrCmp(a, a, 0x0FFFFFFFul, 0));
    show("\"\"    vs \"\",    lcid 0x0FFFFFFF", VarBstrCmp(e, e, 0x0FFFFFFFul, 0));
    show("NULL  vs NULL,  lcid 0x0FFFFFFF", VarBstrCmp(0, 0, 0x0FFFFFFFul, 0));
    show("\"abc\" vs NULL,  lcid 0x0FFFFFFF", VarBstrCmp(a, 0, 0x0FFFFFFFul, 0));
    printf("   CompareStringW with that lcid returns %d, GetLastError %lu\n",
           CompareStringW(0x0FFFFFFFul, 0, L"abc", 3, L"abd", 3), (unsigned long)GetLastError());

    printf("\n== 2. an undefined flag bit ==\n");
    show("\"abc\" vs \"abd\", flags 0x00000040", VarBstrCmp(a, b, LOCALE_USER_DEFAULT, 0x40));
    show("\"abc\" vs \"abc\", flags 0x00000040", VarBstrCmp(a, a, LOCALE_USER_DEFAULT, 0x40));
    show("\"\"    vs \"\",    flags 0x00000040", VarBstrCmp(e, e, LOCALE_USER_DEFAULT, 0x40));
    show("NULL  vs NULL,  flags 0x00000040", VarBstrCmp(0, 0, LOCALE_USER_DEFAULT, 0x40));
    show("\"abc\" vs \"\",    flags 0x00000040", VarBstrCmp(a, e, LOCALE_USER_DEFAULT, 0x40));
    show("\"abc\" vs \"abd\", flags 0x80000000", VarBstrCmp(a, b, LOCALE_USER_DEFAULT, 0x80000000ul));
    printf("   CompareStringW with flag 0x40 returns %d, GetLastError %lu\n",
           CompareStringW(LOCALE_USER_DEFAULT, 0x40, L"abc", 3, L"abd", 3),
           (unsigned long)GetLastError());

    printf("\n== 3. which flag bits are ACCEPTED, derived rather than listed ==\n");
    {
        int bit, n = 0;
        unsigned long mask = 0;
        printf("   accepted: ");
        for (bit = 0; bit < 32; ++bit) {
            DWORD fl = 1ul << bit;
            if (CompareStringW(LOCALE_USER_DEFAULT, fl, L"a", 1, L"b", 1) != 0) {
                printf("%08lX ", (unsigned long)fl);
                mask |= fl;
                ++n;
            }
        }
        printf("\n   %d bit(s), mask %08lX\n", n, mask);
        printf("   and VarBstrCmp agrees on each: ");
        {
            int bad = 0;
            for (bit = 0; bit < 32; ++bit) {
                DWORD fl = 1ul << bit;
                int ok_csw = CompareStringW(LOCALE_USER_DEFAULT, fl, L"a", 1, L"b", 1) != 0;
                HRESULT v = VarBstrCmp(a, b, LOCALE_USER_DEFAULT, fl);
                int ok_var = (v == VARCMP_LT || v == VARCMP_EQ || v == VARCMP_GT);
                if (ok_csw != ok_var) { printf("bit %d DIFFERS ", bit); ++bad; }
            }
            printf("%s\n", bad ? "" : "yes, every bit");
        }
    }

    printf("\n== 4. does the EMPTY case validate the arguments? ==\n");
    printf("   (if \"\" vs \"\" with rubbish flags is EQ, the empty rules come FIRST and a fast\n"
           "    path may do the same; if it is an error, they do not)\n");
    show("\"\" vs \"\", flags 0x00000040", VarBstrCmp(e, e, LOCALE_USER_DEFAULT, 0x40));
    show("\"\" vs \"\", lcid 0x0FFFFFFF", VarBstrCmp(e, e, 0x0FFFFFFFul, 0));
    show("NULL vs \"\", flags 0x00000040", VarBstrCmp(0, e, LOCALE_USER_DEFAULT, 0x40));
    show("\"a\" vs \"\", flags 0x00000040", VarBstrCmp(a, e, LOCALE_USER_DEFAULT, 0x40));

    printf("\n== 5. and does an IDENTICAL non-empty pair validate? ==\n");
    printf("   (this is the fast path's own question: if EQ comes back even with a bad flag,\n"
           "    the export is not validating either and neither need we)\n");
    show("\"abc\" vs \"abc\" (equal content), flags 0x40",
         VarBstrCmp(a, SysAllocString(L"abc"), LOCALE_USER_DEFAULT, 0x40));
    show("x vs x (same pointer), flags 0x40", VarBstrCmp(a, a, LOCALE_USER_DEFAULT, 0x40));

    SysFreeString(a); SysFreeString(b); SysFreeString(e);
    return 0;
}
