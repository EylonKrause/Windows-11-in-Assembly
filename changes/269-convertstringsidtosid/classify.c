/* changes/269-convertstringsidtosid/classify.c
 *
 * The two character classes, built from the OS and never assumed.
 *
 * probes/authfield.c and probes/digits.c established that this one export contains TWO different
 * number parsers, and that neither of them is "the ASCII digits":
 *
 *   the revision and the identifier authority are read by a lenient routine that skips leading
 *   whitespace, and whitespace here is the full Unicode set, U+1680, U+180E, U+2000..U+200A,
 *   U+2028, U+2029, U+202F, U+205F, U+3000 and U+00A0 as well as the ASCII five, takes an
 *   optional single '+', and then accepts the whole unicode decimal digit set. U+0661 (Arabic-Indic
 *   one), U+0967 (Devanagari), U+0E51 (Thai), U+17E1 (Khmer) and a dozen more blocks are each worth
 *   their face value.
 *
 *   a SUB-AUTHORITY is read by a strict routine that accepts no whitespace, no sign, and only the
 *   ASCII digits and the FULLWIDTH digits U+FF10..U+FF19.
 *
 * None of that is transcribed here. Both classes are derived at run time by asking the export
 * itself, which needs no Unicode knowledge at all and cannot drift when a future Windows adds a
 * digit block. The alternative (GetStringTypeW's C1_DIGIT and C1_SPACE) would be a second
 * opinion about what advapi32 does rather than a measurement of it.
 *
 * It takes two questions per code unit, not one, and the reason is worth recording because the
 * first version got it wrong and the self-check caught it. Asking only
 *
 *     S-1-<c>7-1      accepted with authority 7  ->  <c> was skipped
 *                     accepted with authority v*10+7 -> <c> is a digit worth v
 *
 * classifies the character ZERO as whitespace, because a leading zero and a skipped space leave the
 * same number behind. So the candidate is asked again in a position where a skip is impossible:
 *
 *     S-1-1<c>-1      accepted with authority 10+v -> <c> is a digit worth v
 *                     refused                      -> <c> is not a digit at all
 *
 *, trailing whitespace inside a field is refused ("S-1-5 -1" does not parse), so the second
 * question separates the two cleanly, and the first is then only needed to tell whitespace from
 * a character that is simply not allowed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <string.h>

#define CLS_NONE  0xFF
#define CLS_SPACE 0xFE

unsigned char wia_sid_lenient[0x10000];   /* 0..9, CLS_SPACE, or CLS_NONE */
unsigned char wia_sid_strict[0x10000];    /* 0..9 or CLS_NONE */
int wia_sid_lenient_digits, wia_sid_lenient_spaces, wia_sid_strict_digits;

static int try_sid(const wchar_t* s, unsigned long long* auth, unsigned long* sub0)
{
    PSID sid = 0;
    if (!ConvertStringSidToSidW(s, &sid) || !sid) return 0;
    {
        unsigned char* b = (unsigned char*)sid;
        unsigned long long v = 0;
        int i;
        for (i = 0; i < 6; ++i) v = (v << 8) | b[2 + i];
        *auth = v;
        *sub0 = (unsigned long)b[8] | ((unsigned long)b[9] << 8) |
                ((unsigned long)b[10] << 16) | ((unsigned long)b[11] << 24);
    }
    LocalFree(sid);
    return 1;
}

int wia_sid_classify_init(void)
{
    wchar_t s[12];
    unsigned long long a;
    unsigned long u;
    int c;
    memset(wia_sid_lenient, CLS_NONE, sizeof wia_sid_lenient);
    memset(wia_sid_strict, CLS_NONE, sizeof wia_sid_strict);
    wia_sid_lenient_digits = wia_sid_lenient_spaces = wia_sid_strict_digits = 0;

    for (c = 1; c < 0x10000; ++c) {
        /* --- the lenient field --- */
        s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = L'-';
        s[4] = L'1'; s[5] = (wchar_t)c; s[6] = L'-'; s[7] = L'1'; s[8] = 0;
        if (try_sid(s, &a, &u)) {                       /* a digit: no skip is possible here */
            if (a < 10 || a > 19) return 2;
            wia_sid_lenient[c] = (unsigned char)(a - 10);
            ++wia_sid_lenient_digits;
        } else {
            s[4] = (wchar_t)c; s[5] = L'7';
            if (try_sid(s, &a, &u)) {                   /* accepted only when leading: a skip */
                if (a != 7) return 3;
                wia_sid_lenient[c] = CLS_SPACE;
                ++wia_sid_lenient_spaces;
            }
        }

        /* --- the strict field --- */
        s[0] = L'S'; s[1] = L'-'; s[2] = L'1'; s[3] = L'-'; s[4] = L'5'; s[5] = L'-';
        s[6] = L'1'; s[7] = (wchar_t)c; s[8] = 0;
        if (try_sid(s, &a, &u)) {
            if (u < 10 || u > 19) return 4;
            wia_sid_strict[c] = (unsigned char)(u - 10);
            ++wia_sid_strict_digits;
        }
    }

    /* '+' reads as "skipped" because it is a SIGN, and a sign is not whitespace: " +5" is accepted
       while "+ 5" and "++5" are not. The parser must treat it as its own thing, so it is taken out
       of the space class here and handled explicitly. */
    if (wia_sid_lenient[L'+'] != CLS_SPACE) return 5;
    wia_sid_lenient[L'+'] = CLS_NONE;
    --wia_sid_lenient_spaces;

    /* the sanity the numbers themselves have to satisfy. Without these the table can come back
       empty, or classify the digit zero as whitespace, and every one of those is a wrong answer
       that looks like a valid one -- which is what the first version of this file did. */
    if (wia_sid_strict[L'0'] != 0 || wia_sid_strict[L'9'] != 9) return 6;
    if (wia_sid_lenient[L'0'] != 0 || wia_sid_lenient[L'9'] != 9) return 7;
    if (wia_sid_strict[0xFF10] != 0 || wia_sid_strict[0xFF19] != 9) return 8;
    if (wia_sid_lenient[L' '] != CLS_SPACE || wia_sid_lenient[0x3000] != CLS_SPACE) return 9;
    if (wia_sid_strict[L' '] != CLS_NONE || wia_sid_strict[0x0661] != CLS_NONE) return 10;
    if (wia_sid_lenient[0x0661] != 1) return 11;
    if (!wia_sid_lenient_digits || !wia_sid_lenient_spaces || !wia_sid_strict_digits) return 12;
    return 0;
}
