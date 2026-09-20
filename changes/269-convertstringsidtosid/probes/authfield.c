/* changes/269-convertstringsidtosid/probes/authfield.c
 *
 * The identifier authority is parsed by a different routine from the rest.
 *
 * probes/model.c validated the grammar from grammar.c and limits.c against the live export over
 * 36508 cases and found 2822 disagreements, all of one shape:
 *
 *     S-0-+1-0    live: OK      model: ERROR_INVALID_SID
 *
 * A leading `+` is refused in a SUB-AUTHORITY, grammar.c measured that directly, `S-1-5-+18` is
 * rejected, and accepted in the IDENTIFIER AUTHORITY. The two fields are not parsed by the same
 * code, which is not something any amount of reading would have suggested, and it is exactly the
 * kind of asymmetry this project has been caught by before (change 268's two directions).
 *
 * So the authority field is characterised on its own: signs, whitespace, hex-with-sign, and what
 * the resulting six bytes actually contain. The sub-authority and revision fields are asked the
 * same questions alongside, so the difference is visible rather than inferred.
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
        LPWSTR t = 0;
        printf("OK   bytes:");
        for (i = 0; i < len && i < 16; ++i) printf(" %02X", b[i]);
        if (ConvertSidToStringSidW(sid, &t)) { printf("   -> %ls", t); LocalFree(t); }
        printf("\n");
        LocalFree(sid);
    } else {
        printf("NO   err=%lu\n", (unsigned long)GetLastError());
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== the AUTHORITY field (the second number) ==\n");
    ask(L"S-1-5-1");
    ask(L"S-1-+5-1");
    ask(L"S-1--5-1");
    ask(L"S-1- 5-1");
    ask(L"S-1-  5-1");
    ask(L"S-1-\t5-1");
    ask(L"S-1-+0x5-1");
    ask(L"S-1- +5-1");
    ask(L"S-1-+ 5-1");
    ask(L"S-1-++5-1");
    ask(L"S-1-5 -1");
    ask(L"S-1-+281474976710655-1");
    ask(L"S-1-+281474976710656-1");
    ask(L"S-1-+4294967296-1");

    printf("\n== the SUB-AUTHORITY field (the third and later) ==\n");
    ask(L"S-1-5-+1");
    ask(L"S-1-5--1");
    ask(L"S-1-5- 1");
    ask(L"S-1-5-\t1");
    ask(L"S-1-5-+0x1");
    ask(L"S-1-5-1 ");
    ask(L"S-1-5-1-+2");
    ask(L"S-1-5-1- 2");

    printf("\n== the REVISION field (the first number) ==\n");
    ask(L"S-+1-5-1");
    ask(L"S- 1-5-1");
    ask(L"S-\t1-5-1");
    ask(L"S--1-5-1");
    ask(L"S-+0x1-5-1");

    printf("\n== and the prefix itself ==\n");
    ask(L"S -1-5-1");
    ask(L" S-1-5-1");
    ask(L"\tS-1-5-1");

    printf("\n== what a NEGATIVE authority becomes, if it is taken at all ==\n");
    ask(L"S-1--1-1");
    ask(L"S-1--0x1-1");

    /* Which characters count as leading whitespace. "space and tab" is what two examples showed;
       the SET is what matters, and it is cheaper to enumerate it than to assume it is iswspace. */
    printf("\n== every code unit 1..0xFFFF as a leading character in the AUTHORITY field ==\n");
    {
        wchar_t t[16];
        int c, n = 0;
        printf("   accepted: ");
        for (c = 1; c < 0x10000; ++c) {
            PSID sid = 0;
            t[0] = L'S'; t[1] = L'-'; t[2] = L'1'; t[3] = L'-';
            t[4] = (wchar_t)c;
            t[5] = L'5'; t[6] = L'-'; t[7] = L'1'; t[8] = 0;
            if (ConvertStringSidToSidW(t, &sid) && sid) {
                printf("%04X ", c);
                if (++n % 12 == 0) printf("\n             ");
                LocalFree(sid);
            }
        }
        printf("\n   %d code units are accepted before the number\n", n);
    }

    printf("\n== and the same sweep in a SUB-AUTHORITY, which should accept none ==\n");
    {
        wchar_t t[16];
        int c, n = 0;
        printf("   accepted: ");
        for (c = 1; c < 0x10000; ++c) {
            PSID sid = 0;
            t[0] = L'S'; t[1] = L'-'; t[2] = L'1'; t[3] = L'-'; t[4] = L'5'; t[5] = L'-';
            t[6] = (wchar_t)c;
            t[7] = L'1'; t[8] = 0;
            if (ConvertStringSidToSidW(t, &sid) && sid) { printf("%04X ", c); ++n; LocalFree(sid); }
        }
        printf("\n   %d code units are accepted before a sub-authority\n", n);
    }
    return 0;
}
