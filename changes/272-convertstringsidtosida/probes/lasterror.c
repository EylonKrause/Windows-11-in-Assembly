/* changes/272-convertstringsidtosida/probes/lasterror.c
 *
 * Does a successful parse zero the last error? And did change 269's gate ever ask?
 *
 * This change's correctness gate reported 1106 mismatches on its first run, every one of them the
 * same shape:
 *
 *     "S-1-5-1"   live: TRUE, GetLastError() == 0        ours and the model: TRUE, err untouched
 *
 * So ConvertStringSidToSidA zeroes the last error on success. That is the same rule change 271
 * measured for ConvertSidToStringSidA and change 270 for its wide sibling -- but change 269
 * concluded the opposite for ConvertStringSidToSidW, because its implementation does not touch the
 * last error on success and its gate agreed with the live export over 429776 cases.
 *
 * The reason that proves nothing is that its gate set the last error to zero before every call:
 *
 *     SetLastError(0); rb = wia_str2sid(s, &b); eb = GetLastError();
 *
 * With a pre-value of zero, "left untouched" and "set to zero" produce the same reading. 429776
 * cases cannot distinguish them, and no number of further cases would. It is the same defect as
 * change 067's corpus stepping MaximumLength by two: the generator could not express the case.
 *
 * So this file asks all four exports of the family the question properly -- from a NON-ZERO
 * starting value, on the succeeding path and on each failing one -- and the answer decides whether
 * change 269 has a latent defect that its own gate is blind to.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

#define SENTINEL 0xD15EA5E

static void ask_w(const char* what, const wchar_t* s)
{
    static const DWORD PRE[] = { 0, 1, 87, SENTINEL, ERROR_INVALID_SID, 0xFFFFFFFF };
    unsigned i;
    printf("  ConvertStringSidToSidW(%-22s)", what);
    for (i = 0; i < sizeof PRE / sizeof PRE[0]; ++i) {
        PSID p = 0;
        BOOL r;
        DWORD after;
        SetLastError(PRE[i]);
        r = ConvertStringSidToSidW(s, &p);
        after = GetLastError();
        printf("  %s%lu->%lu", r ? "" : "!", (unsigned long)PRE[i], (unsigned long)after);
        if (p) LocalFree(p);
    }
    printf("\n");
}

static void ask_a(const char* what, const char* s)
{
    static const DWORD PRE[] = { 0, 1, 87, SENTINEL, ERROR_INVALID_SID, 0xFFFFFFFF };
    unsigned i;
    printf("  ConvertStringSidToSidA(%-22s)", what);
    for (i = 0; i < sizeof PRE / sizeof PRE[0]; ++i) {
        PSID p = 0;
        BOOL r;
        DWORD after;
        SetLastError(PRE[i]);
        r = ConvertStringSidToSidA(s, &p);
        after = GetLastError();
        printf("  %s%lu->%lu", r ? "" : "!", (unsigned long)PRE[i], (unsigned long)after);
        if (p) LocalFree(p);
    }
    printf("\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== the last error across a call, from six starting values ==\n");
    printf("   \"before->after\"; a leading ! means the call returned FALSE\n\n");

    printf("-- the SUCCEEDING path --\n");
    ask_w("S-1-5-1",   L"S-1-5-1");
    ask_a("S-1-5-1",   "S-1-5-1");
    ask_w("a 5-sub SID", L"S-1-5-21-305419896-2596069104-287454020-1001");
    ask_a("a 5-sub SID", "S-1-5-21-305419896-2596069104-287454020-1001");
    ask_w("the alias BA", L"BA");
    ask_a("the alias BA", "BA");

    printf("\n-- the FAILING paths --\n");
    ask_w("not-a-sid",  L"not-a-sid");
    ask_a("not-a-sid",  "not-a-sid");
    ask_w("an SDDL terminator", L"S-1-5-1)");
    ask_a("an SDDL terminator", "S-1-5-1)");
    ask_w("255 sub-authorities",
          L"S-1-5-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1"
          L"-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1"
          L"-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1"
          L"-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1"
          L"-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1"
          L"-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1"
          L"-1-1-1-1-1-1-1-1-1-1-1-1-1");

    printf("\n-- and the two formatting exports, for comparison --\n");
    {
        static const DWORD PRE[] = { 0, 1, 87, SENTINEL, ERROR_INVALID_SID, 0xFFFFFFFF };
        unsigned char sid[12] = { 1, 1, 0,0,0,0,0,5, 18,0,0,0 };
        unsigned i;
        printf("  ConvertSidToStringSidW(a 1-sub SID    )");
        for (i = 0; i < sizeof PRE / sizeof PRE[0]; ++i) {
            LPWSTR p = 0;
            BOOL r;
            SetLastError(PRE[i]);
            r = ConvertSidToStringSidW((PSID)sid, &p);
            printf("  %s%lu->%lu", r ? "" : "!", (unsigned long)PRE[i], (unsigned long)GetLastError());
            if (p) LocalFree(p);
        }
        printf("\n  ConvertSidToStringSidA(a 1-sub SID    )");
        for (i = 0; i < sizeof PRE / sizeof PRE[0]; ++i) {
            LPSTR p = 0;
            BOOL r;
            SetLastError(PRE[i]);
            r = ConvertSidToStringSidA((PSID)sid, &p);
            printf("  %s%lu->%lu", r ? "" : "!", (unsigned long)PRE[i], (unsigned long)GetLastError());
            if (p) LocalFree(p);
        }
        printf("\n");
    }
    return 0;
}
