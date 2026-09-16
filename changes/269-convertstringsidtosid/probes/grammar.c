/* changes/269-convertstringsidtosid/probes/grammar.c
 *
 * WHAT DOES advapi32!ConvertStringSidToSidW ACTUALLY ACCEPT?
 *
 * discovery/sid_inet_bstr.c measured it at 275.78 ns, about 45 ns per decimal number, and that is
 * the reason to build it. What it ACCEPTS is a separate question, and it cannot be answered from
 * the documentation: the documented form is `S-R-I-S-S…`, and the export also takes the two-letter
 * SDDL aliases, a hexadecimal identifier authority, and -- as this file establishes -- several
 * things a careful reading would have said it refuses.
 *
 * Every line prints the input, the verdict, `GetLastError()` on refusal, and the SID formatted back
 * to a string on acceptance. Nothing here is asserted; it is all read off the live export, one
 * hand-built case at a time, so that the table can be checked by eye rather than trusted.
 *
 * THE QUESTIONS, in the order they decide the implementation:
 *
 *   1. the GRAMMAR -- which prefixes, separators, and counts are legal
 *   2. the NUMBER FORMATS -- decimal, hexadecimal, leading zeros, leading signs, overflow
 *   3. the IDENTIFIER AUTHORITY, which is 48 bits and therefore not a DWORD
 *   4. CASE and WHITESPACE
 *   5. the FAILURE CODES, which a caller switches on
 *   6. the ALIASES, one of which resolves through the local machine and so cannot be a constant
 *   7. the ALLOCATION -- what the returned block is, and what frees it
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static int cases;

static void ask(const wchar_t* in, const char* what)
{
    PSID sid = 0;
    LPWSTR back = 0;
    DWORD err;
    BOOL ok;
    ++cases;
    SetLastError(0);
    ok = ConvertStringSidToSidW(in, &sid);
    err = GetLastError();
    printf("  %-34ls ", in);
    if (ok && sid) {
        ConvertSidToStringSidW(sid, &back);
        printf("OK    %-46ls len=%-3lu  %s\n", back ? back : L"(unprintable)",
               (unsigned long)GetLengthSid(sid), what);
        if (back) LocalFree(back);
        LocalFree(sid);
    } else {
        printf("NO    err=%-5lu %-40s %s\n", (unsigned long)err, "", what);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== ConvertStringSidToSidW: what it accepts ==\n");
    printf("  %-34s %-5s %-46s %s\n", "input", "", "formatted back", "note");

    printf("\n-- 1. the grammar --\n");
    ask(L"S-1-5-18",            "the documented form");
    ask(L"S-1-5",               "no sub-authorities at all");
    ask(L"S-1-0",               "authority 0");
    ask(L"S-1",                 "revision and authority only");
    ask(L"S-",                  "nothing after the prefix");
    ask(L"S",                   "the prefix alone");
    ask(L"",                    "empty");
    ask(L"1-5-18",              "no S");
    ask(L"S-2-5-18",            "revision 2");
    ask(L"S-0-5-18",            "revision 0");
    ask(L"S-1-5-18-",           "a trailing separator");
    ask(L"S-1-5--18",           "a doubled separator");
    ask(L"S-1-5-18-19-20-21-22-23-24-25-26-27-28-29-30-31", "fifteen sub-authorities");
    ask(L"S-1-5-18-19-20-21-22-23-24-25-26-27-28-29-30-31-32", "SIXTEEN sub-authorities");

    printf("\n-- 2. the number formats --\n");
    ask(L"S-1-5-0x12",          "hexadecimal sub-authority");
    ask(L"S-1-5-012",           "a leading zero: octal, or decimal 12?");
    ask(L"S-1-5-+18",           "a leading plus");
    ask(L"S-1-5--18",           "a leading minus (also a doubled separator)");
    ask(L"S-1-5-4294967295",    "the largest DWORD");
    ask(L"S-1-5-4294967296",    "one past it");
    ask(L"S-1-5-99999999999999999999", "far past it");
    ask(L"S-1-5-0xFFFFFFFF",    "the largest DWORD, in hex");
    ask(L"S-1-5-0x100000000",   "one past it, in hex");
    ask(L"S-1-5-1 8",           "an embedded space");
    ask(L"S-1-5-1a",            "a trailing letter");

    printf("\n-- 3. the identifier authority, which is 48 bits --\n");
    ask(L"S-1-4294967295-1",    "the largest DWORD as the authority");
    ask(L"S-1-4294967296-1",    "past a DWORD, but inside 48 bits");
    ask(L"S-1-281474976710655-1", "the largest 48-bit value");
    ask(L"S-1-281474976710656-1", "one past 48 bits");
    ask(L"S-1-0x123456789ABC-1", "the same, in hex");
    ask(L"S-1-0xFFFFFFFFFFFF-1", "the largest, in hex");
    ask(L"S-1-0x1000000000000-1", "one past it, in hex");

    printf("\n-- 4. case and whitespace --\n");
    ask(L"s-1-5-18",            "a lower-case prefix");
    ask(L" S-1-5-18",           "a leading space");
    ask(L"S-1-5-18 ",           "a trailing space");
    ask(L"S-1-5-0X12",          "an upper-case hex marker");
    ask(L"S-1-5-0xAB",          "lower-case hex digits");
    ask(L"S-1-5-0xab",          "the same, lower case");

    printf("\n-- 5. the aliases --\n");
    {
        static const wchar_t* AL[] = { L"BA", L"SY", L"WD", L"AU", L"LA", L"NU", L"IU", L"AN",
                                       L"ba", L"Ba", L"ZZ", L"B", L"BAA" };
        int i;
        for (i = 0; i < 13; ++i)
            ask(AL[i], i < 8 ? "a documented alias" :
                       i < 10 ? "the same alias, different case" : "not an alias");
    }

    printf("\n-- 6. the allocation --\n");
    {
        PSID sid = 0;
        if (ConvertStringSidToSidW(L"S-1-5-21-1-2-3-1001", &sid) && sid) {
            SIZE_T ls = LocalSize(sid);
            DWORD len = GetLengthSid(sid);
            printf("  a 5-sub-authority SID: GetLengthSid = %lu, LocalSize = %Iu, LocalFlags = %X\n",
                   (unsigned long)len, ls, LocalFlags(sid));
            printf("  %s\n", ls >= len ? "  LocalSize answers, so the block IS a LocalAlloc block"
                                       : "  LocalSize does not answer: NOT a LocalAlloc block");
            LocalFree(sid);
        }
        {
            /* and can a block allocated BY HAND the same way be freed by the caller's LocalFree? */
            PSID mine = (PSID)LocalAlloc(LMEM_FIXED, 32);
            HLOCAL r;
            if (mine) {
                memset(mine, 0, 32);
                r = LocalFree(mine);
                printf("  LocalFree on a hand-allocated LMEM_FIXED block returned %p (NULL = freed)\n",
                       (void*)r);
            }
        }
    }

    printf("\n  %d cases\n", cases);
    return 0;
}
