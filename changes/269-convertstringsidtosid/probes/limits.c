/* changes/269-convertstringsidtosid/probes/limits.c
 *
 * THE EDGES probes/grammar.c raised but did not settle.
 *
 * grammar.c established that the export is more permissive than its documentation in several ways
 * and stricter in others, and three of its answers demand a follow-up before a line of assembly is
 * written:
 *
 *   * it accepted SIXTEEN sub-authorities, and a SID is documented to hold fifteen. Where does it
 *     actually stop, and what does the byte in the structure say?
 *   * it accepted revision 0 and revision 2, and `ConvertSidToStringSidW` then refused to format
 *     the result -- so the revision is STORED rather than validated. What is the legal range?
 *   * a sub-authority SATURATES on overflow (4294967296 becomes 4294967295) while the identifier
 *     authority REFUSES. That asymmetry is exactly the kind of thing a reimplementation gets wrong
 *     by making the two consistent.
 *
 * Every line prints the raw bytes of the SID as well as its formatted form, because the formatted
 * form is produced by a DIFFERENT function that has its own opinions -- and grammar.c already
 * caught it declining to print a SID the parser had happily produced.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

static void dump(const wchar_t* in)
{
    PSID sid = 0;
    LPWSTR back = 0;
    BOOL ok;
    SetLastError(0);
    ok = ConvertStringSidToSidW(in, &sid);
    printf("  %-56ls ", in);
    if (!ok || !sid) { printf("NO   err=%lu\n", (unsigned long)GetLastError()); return; }
    {
        unsigned char* b = (unsigned char*)sid;
        DWORD len = GetLengthSid(sid);
        DWORD i;
        printf("OK   rev=%u count=%u len=%-3lu  bytes:", b[0], b[1], (unsigned long)len);
        for (i = 0; i < len && i < 16; ++i) printf(" %02X", b[i]);
        if (len > 16) printf(" ..");
        if (ConvertSidToStringSidW(sid, &back)) { printf("  -> %ls", back); LocalFree(back); }
        else printf("  -> the formatter REFUSES it (%lu)", (unsigned long)GetLastError());
        printf("\n");
    }
    LocalFree(sid);
}

int main(void)
{
    wchar_t buf[600];
    int k;
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== how many sub-authorities? ==\n");
    for (k = 1; k <= 20; ++k) {
        int i, n = 0;
        n += wsprintfW(buf + n, L"S-1-5");
        for (i = 0; i < k; ++i) n += wsprintfW(buf + n, L"-%d", i + 1);
        dump(buf);
    }

    printf("\n== the revision byte: stored, or validated? ==\n");
    for (k = 0; k <= 3; ++k) { wsprintfW(buf, L"S-%d-5-18", k); dump(buf); }
    dump(L"S-255-5-18");
    dump(L"S-256-5-18");
    dump(L"S-4294967295-5-18");
    dump(L"S-4294967296-5-18");
    dump(L"S-0x10-5-18");

    printf("\n== saturation: which fields do it, and which refuse? ==\n");
    dump(L"S-1-5-4294967295");
    dump(L"S-1-5-4294967296");
    dump(L"S-1-5-18446744073709551616");
    dump(L"S-1-5-0xFFFFFFFFF");
    dump(L"S-1-4294967296-1");
    dump(L"S-1-281474976710655-1");
    dump(L"S-1-281474976710656-1");
    dump(L"S-1-0x1000000000000-1");

    printf("\n== a few more shapes the grammar did not cover ==\n");
    dump(L"S-1--1");
    dump(L"S-1-5-18-0");
    dump(L"S-1-5-0");
    dump(L"S-1-5-0x0");
    dump(L"S-1-5-00000000000000000000018");
    dump(L"S-1-5-0x0000000000000012");
    dump(L"S-1-0-0");
    dump(L"S-1-x-1");
    dump(L"S-1-5-x");
    dump(L"S-1-5-0x");
    dump(L"S-x-5-18");
    dump(L"SS-1-5-18");
    dump(L"S--1-5-18");

    printf("\n== and what the ALIASES really are ==\n");
    {
        /* every two-letter combination, so the table is enumerated rather than recalled */
        int a, b, found = 0;
        printf("  ");
        for (a = 0; a < 26; ++a) {
            for (b = 0; b < 26; ++b) {
                PSID sid = 0;
                wchar_t al[3];
                al[0] = (wchar_t)(L'A' + a); al[1] = (wchar_t)(L'A' + b); al[2] = 0;
                if (ConvertStringSidToSidW(al, &sid) && sid) {
                    LPWSTR t = 0;
                    ConvertSidToStringSidW(sid, &t);
                    printf("%ls=%ls  ", al, t ? t : L"?");
                    if (t) LocalFree(t);
                    LocalFree(sid);
                    if (++found % 4 == 0) printf("\n  ");
                }
            }
        }
        printf("\n  %d two-letter aliases exist\n", found);
    }
    return 0;
}
