/* changes/269-convertstringsidtosid/probes/basecarry.c
 *
 * The base is not per-field.
 *
 * probes/model.c, with both number parsers in place, was down to 1420 disagreements out of 36508
 * and every one of them had a HEXADECIMAL REVISION:
 *
 *     S-0x0-0-18      live: sub-authority 0x18 = 24      model: 18
 *     S-0x0-0-1a      live: accepted                     model: ERROR_INVALID_SID
 *
 * "18" is decimal in `S-1-5-18` and hexadecimal in `S-0x0-0-18`. The `0x` seen in one field changes
 * how a LATER field is read, which is not a grammar at all; it is a parser carrying state across
 * fields, and nothing in the documentation or in any of the four earlier probes hinted at it.
 *
 * This file establishes exactly how far that state carries: which field can set it, whether it can
 * be set twice, whether it can be turned back off, and whether the strict sub-authority parser is
 * affected as well as the lenient one.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static void ask(const wchar_t* in)
{
    PSID sid = 0;
    printf("  %-30ls ", in);
    if (ConvertStringSidToSidW(in, &sid) && sid) {
        unsigned char* b = (unsigned char*)sid;
        DWORD len = GetLengthSid(sid), i;
        unsigned long long auth = 0;
        printf("OK  rev=%-3u", b[0]);
        for (i = 0; i < 6; ++i) auth = (auth << 8) | b[2 + i];
        printf(" auth=%-8llu subs:", auth);
        for (i = 8; i + 3 < len; i += 4)
            printf(" %lu", (unsigned long)(b[i] | ((unsigned long)b[i+1] << 8) |
                                           ((unsigned long)b[i+2] << 16) | ((unsigned long)b[i+3] << 24)));
        printf("\n");
        LocalFree(sid);
    } else {
        printf("NO  err=%lu\n", (unsigned long)GetLastError());
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== the control: no hex anywhere ==\n");
    ask(L"S-1-5-18");
    ask(L"S-1-5-1a");

    printf("\n== a hex REVISION ==\n");
    ask(L"S-0x1-5-18");
    ask(L"S-0x1-5-1a");
    ask(L"S-0x1-10-10");

    printf("\n== a hex AUTHORITY ==\n");
    ask(L"S-1-0x5-18");
    ask(L"S-1-0x5-1a");
    ask(L"S-1-0x10-10");

    printf("\n== a hex SUB-AUTHORITY, and what follows it ==\n");
    ask(L"S-1-5-0x10-10");
    ask(L"S-1-5-10-0x10");
    ask(L"S-1-5-0x1a-1a");
    ask(L"S-1-5-10-1a");

    printf("\n== can it be set twice, or turned off? ==\n");
    ask(L"S-0x1-0x5-18");
    ask(L"S-0x1-5-0x18");
    ask(L"S-1-5-0x10-0x10");

    printf("\n== does the hex state also relax what the STRICT field accepts? ==\n");
    ask(L"S-0x1-5-\xFF11\xFF12");
    ask(L"S-1-5-\xFF11\xFF12");
    ask(L"S-0x1-5- 1");
    ask(L"S-0x1-5-+1");

    printf("\n== and does it change the SATURATION limits? ==\n");
    ask(L"S-1-5-4294967296");
    ask(L"S-0x1-5-FFFFFFFFF");
    ask(L"S-0x1-5-FFFFFFFF");
    ask(L"S-0x1-0x1000000000000-1");
    ask(L"S-0x1-FFFFFFFFFFFF-1");
    ask(L"S-0x1-1000000000000-1");
    return 0;
}
