/* changes/269-convertstringsidtosid/probes/digits.c
 *
 * Are those code units digits, or whitespace?
 *
 * probes/authfield.c swept every code unit 1..0xFFFF as the first character of each field and found
 * that the identifier authority accepts 206 of them while a sub-authority accepts 20. The twenty
 * are the ASCII digits and the FULLWIDTH digits, which is exactly what Windows' `iswdigit` answers.
 * The 206 include Unicode whitespace AND several other digit blocks -- Arabic-Indic, Devanagari,
 * Thai, Lao, Tibetan, Myanmar, Khmer, Mongolian -- and the sweep cannot tell the two apart, because
 * a leading ZERO and a skipped SPACE both leave the same number behind.
 *
 * A NON-ZERO digit tells them apart. With `S-1-<c>7-1`:
 *
 *     if <c> is whitespace          the authority is 7
 *     if <c> is the digit ONE       the authority is 17
 *
 * and the six authority bytes say which. Every candidate is asked that way, and the sub-authority
 * is asked the same question with `S-1-5-<c>7` so the two parsers are compared rather than assumed
 * to differ only where the sweep happened to look.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static unsigned long long auth_of(PSID sid)
{
    unsigned char* b = (unsigned char*)sid;
    unsigned long long v = 0;
    int i;
    for (i = 0; i < 6; ++i) v = (v << 8) | b[2 + i];
    return v;
}

static void probe(int c)
{
    wchar_t s[16];
    PSID sid = 0;
    const char* verdict;
    unsigned long long v = 0;
    int sub_ok;

    s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = L'-';
    s[4] = (wchar_t)c; s[5] = L'7'; s[6] = L'-'; s[7] = L'1'; s[8] = 0;
    if (ConvertStringSidToSidW(s, &sid) && sid) {
        v = auth_of(sid);
        LocalFree(sid);
        verdict = (v == 7) ? "SKIPPED (whitespace)"
                : (v == 17) ? "a digit worth ONE"
                : (v == 7 + 10 * 0) ? "?" : "a digit, other value";
    } else {
        verdict = "refused";
    }

    s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = L'-'; s[4] = L'5'; s[5] = L'-';
    s[6] = (wchar_t)c; s[7] = L'7'; s[8] = 0;
    sid = 0;
    sub_ok = ConvertStringSidToSidW(s, &sid) && sid != 0;
    if (sub_ok) {
        unsigned char* b = (unsigned char*)sid;
        unsigned long sv = b[8] | ((unsigned long)b[9] << 8) |
                           ((unsigned long)b[10] << 16) | ((unsigned long)b[11] << 24);
        printf("  U+%04X  authority: %-22s (%llu)   sub-authority: accepted, value %lu\n",
               c, verdict, v, sv);
        LocalFree(sid);
    } else {
        printf("  U+%04X  authority: %-22s (%llu)   sub-authority: refused\n", c, verdict, v);
    }
}

int main(void)
{
    static const int CAND[] = {
        0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x0020, 0x002B, 0x00A0,
        0x0031,                                  /* ASCII one, the control */
        0x0661, 0x06F1, 0x0967, 0x09E7, 0x0A67, 0x0B67, 0x0C67, 0x0E51, 0x0ED1,
        0x0F21, 0x1041, 0x17E1, 0x1811, 0xFF11,
        0x1680, 0x180E, 0x2000, 0x2007, 0x200A, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000
    };
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== is it a digit, or is it whitespace? ==\n");
    printf("   `S-1-<c>7-1`: authority 7 means <c> was skipped, 17 means it was the digit ONE\n\n");
    for (i = 0; i < (int)(sizeof CAND / sizeof CAND[0]); ++i) probe(CAND[i]);

    printf("\n== and how far do the ACCEPTED digits carry? ==\n");
    {
        static const wchar_t* T[] = {
            L"S-1-\xFF11\xFF12-1", L"S-1-5-\xFF11\xFF12", L"S-1-\x0661\x0662-1",
            L"S-1-0\xFF18-1", L"S-1-5-0\xFF18", L"S-1-0x\xFF11-1", L"S-1-5-0x\xFF11"
        };
        for (i = 0; i < 7; ++i) {
            PSID sid = 0;
            LPWSTR t = 0;
            printf("  %-26ls ", T[i]);
            if (ConvertStringSidToSidW(T[i], &sid) && sid) {
                ConvertSidToStringSidW(sid, &t);
                printf("OK  -> %ls\n", t ? t : L"(unprintable)");
                if (t) LocalFree(t);
                LocalFree(sid);
            } else printf("NO  err=%lu\n", (unsigned long)GetLastError());
        }
    }
    return 0;
}
