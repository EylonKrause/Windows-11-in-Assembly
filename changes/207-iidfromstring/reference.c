// changes/207-iidfromstring/reference.c
// The correctness oracle for combase!IIDFromString. Not fast; just obviously right.
//
// THE POINT OF THIS FILE IS THE FAILURE PATH. IIDFromString writes into the caller's GUID as it
// parses, so a malformed string leaves a PARTIALLY FILLED GUID that has to be reproduced byte for
// byte. probes/wmask.c measured that directly -- corrupt exactly one character, then report which of
// the sixteen output bytes moved away from a poison fill -- and this is the rule that came back:
//
//     corrupted char   bytes written        because
//     0   '{'          none                 the brace is checked before anything is stored
//     1..8             0-3, PARTIAL Data1   Data1 is zeroed first, then re-stored after EVERY digit
//     9   '-'          0-3, full Data1
//     10..13           0-3                  Data2 is not stored yet
//     14  '-'          0-3                  ...not even after its four digits validate
//     15..18           0-5                  Data2 was stored once its separator validated
//     19  '-'          0-5
//     20..21           0-7                  Data3 stored after ITS separator
//     22..23           0-8                  Data4[0] stored right after its digits (no separator)
//     24  '-'          0-8                  Data4[1] waits for the separator at 24
//     25..36           one more byte per hex pair
//     37  '}'          all 16               Data4[7] is stored BEFORE the brace is checked
//
// So each field is stored only once IT AND ITS TRAILING SEPARATOR validate -- except Data1, which is
// progressive, and Data4[0], which has no trailing separator.
//
// TWO ERROR CODES, not interchangeable:
//   * 0x80070057 E_INVALIDARG  -- lpiid is NULL, or strlen != 38 exactly. Nothing written.
//   * 0x800401F4 CO_E_IIDSTRING -- length was right, content was not. Partial writes as above.
//
// lpsz == NULL is SUCCESS: write the nil GUID, return S_OK.
#include <windows.h>

#define H_INVALIDARG 0x80070057L
#define H_IIDSTRING  0x800401F4L

static int hexv(unsigned c, unsigned* out){
    if (c >= '0' && c <= '9') { *out = c - '0';      return 1; }
    if (c >= 'A' && c <= 'F') { *out = c - 'A' + 10; return 1; }
    if (c >= 'a' && c <= 'f') { *out = c - 'a' + 10; return 1; }
    return 0;
}

long ref_iidfromstring(const wchar_t* s, GUID* out)
{
    unsigned char* g = (unsigned char*)out;
    unsigned acc, v;
    int i, n;

    if (out == 0) return H_INVALIDARG;
    if (s == 0) { for (i = 0; i < 16; ++i) g[i] = 0; return 0; }

    /* the length must be exactly 38 -- walked, so a shorter string is never read past */
    n = 0;
    while (n < 39 && s[n]) ++n;
    if (n != 38) return H_INVALIDARG;

    if (s[0] != L'{') return H_IIDSTRING;

    /* ---- Data1: zeroed first, then re-stored after every accepted digit ---- */
    acc = 0;
    g[0] = g[1] = g[2] = g[3] = 0;
    for (i = 1; i <= 8; ++i) {
        if (!hexv((unsigned)s[i], &v)) return H_IIDSTRING;
        acc = (acc << 4) | v;
        g[0] = (unsigned char)(acc      );      /* little-endian Data1, rewritten each digit */
        g[1] = (unsigned char)(acc >>  8);
        g[2] = (unsigned char)(acc >> 16);
        g[3] = (unsigned char)(acc >> 24);
    }

    /* ---- Data2: chars 10..13, stored only after the separator at 14 ---- */
    if (s[9] != L'-') return H_IIDSTRING;
    acc = 0;
    for (i = 10; i <= 13; ++i) {
        if (!hexv((unsigned)s[i], &v)) return H_IIDSTRING;
        acc = (acc << 4) | v;
    }
    if (s[14] != L'-') return H_IIDSTRING;      /* Data2 still unwritten if this fails */
    g[4] = (unsigned char)(acc); g[5] = (unsigned char)(acc >> 8);

    /* ---- Data3: chars 15..18, stored only after the separator at 19 ---- */
    acc = 0;
    for (i = 15; i <= 18; ++i) {
        if (!hexv((unsigned)s[i], &v)) return H_IIDSTRING;
        acc = (acc << 4) | v;
    }
    if (s[19] != L'-') return H_IIDSTRING;
    g[6] = (unsigned char)(acc); g[7] = (unsigned char)(acc >> 8);

    /* ---- Data4[0]: chars 20..21, no trailing separator, stored at once ---- */
    acc = 0;
    for (i = 20; i <= 21; ++i) {
        if (!hexv((unsigned)s[i], &v)) return H_IIDSTRING;
        acc = (acc << 4) | v;
    }
    g[8] = (unsigned char)acc;

    /* ---- Data4[1]: chars 22..23, waits for the separator at 24 ---- */
    acc = 0;
    for (i = 22; i <= 23; ++i) {
        if (!hexv((unsigned)s[i], &v)) return H_IIDSTRING;
        acc = (acc << 4) | v;
    }
    if (s[24] != L'-') return H_IIDSTRING;
    g[9] = (unsigned char)acc;

    /* ---- Data4[2..7]: chars 25..36, a byte per pair, each stored immediately ---- */
    for (n = 0; n < 6; ++n) {
        int a = 25 + n * 2;
        acc = 0;
        if (!hexv((unsigned)s[a],     &v)) return H_IIDSTRING;
        acc = v;
        if (!hexv((unsigned)s[a + 1], &v)) return H_IIDSTRING;
        acc = (acc << 4) | v;
        g[10 + n] = (unsigned char)acc;
    }

    /* ---- the closing brace is checked LAST, after all sixteen bytes are stored ---- */
    if (s[37] != L'}') return H_IIDSTRING;
    return 0;
}
