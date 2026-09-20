// changes/208-uuidfromstringw/reference.c
// The correctness oracle for rpcrt4!UuidFromStringW. Not fast; just obviously right.
//
// Contract, identical to change 205's narrow form in every respect and confirmed as such:
// probes/ufs.c drove both live exports over 200 000 generated strings, valid, corrupted and
// truncated, and found 0 return-value differences and 0 output differences.
//   * exactly 36 characters, UNBRACED: 8 hex, '-', 4 hex, '-', 4 hex, '-', 4 hex, '-', 12 hex, then
//     a NUL at [36]. Hex is case-insensitive;
//   * a BRACED string is REJECTED with 1705 (RPC_S_INVALID_STRING_UUID), the opposite of change
//     118's ntdll!RtlGUIDFromString, which requires the braces;
//   * StringUuid == NULL is SUCCESS: return 0 and write the nil UUID (16 zero bytes);
//   * every other malformed input -> 1705 with the output GUID NOT TOUCHED.
//
// Note on width: hexval takes an `unsigned`, not a char. Truncating a UTF-16 cell to a byte would
// accept U+0130 as '0' and U+FF21 as '!', which is exactly the bug the implementation's saturating
// vpackuswb narrowing is designed to avoid, so the oracle must not make that mistake either.
#include <windows.h>

#define RPC_OK       0
#define RPC_BAD_UUID 1705

static int hexval(unsigned c, unsigned* out){
    if (c >= '0' && c <= '9') { *out = c - '0';      return 1; }
    if (c >= 'a' && c <= 'f') { *out = c - 'a' + 10; return 1; }
    if (c >= 'A' && c <= 'F') { *out = c - 'A' + 10; return 1; }
    return 0;
}

long ref_uuidfromstringw(wchar_t* s, GUID* out)
{
    unsigned char b[16];
    unsigned char* g = (unsigned char*)out;
    /* character offsets of the two hex digits making up each GUID byte */
    static const int PAIR[16][2] = {
        {6,7}, {4,5}, {2,3}, {0,1},
        {11,12}, {9,10},
        {16,17}, {14,15},
        {19,20}, {21,22},
        {24,25}, {26,27}, {28,29}, {30,31}, {32,33}, {34,35}
    };
    int i;

    if (s == 0) {                        /* a null pointer is the nil uuid, and it succeeds */
        for (i = 0; i < 16; ++i) g[i] = 0;
        return RPC_OK;
    }

    /* exactly 36 characters, walked, so a shorter string is never read past */
    {
        int n = 0;
        while (n < 37 && s[n]) ++n;
        if (n != 36) return RPC_BAD_UUID;
    }

    if (s[8] != L'-' || s[13] != L'-' || s[18] != L'-' || s[23] != L'-') return RPC_BAD_UUID;

    for (i = 0; i < 16; ++i) {
        unsigned hi, lo;
        if (!hexval((unsigned)s[PAIR[i][0]], &hi)) return RPC_BAD_UUID;
        if (!hexval((unsigned)s[PAIR[i][1]], &lo)) return RPC_BAD_UUID;
        b[i] = (unsigned char)((hi << 4) | lo);
    }

    for (i = 0; i < 16; ++i) g[i] = b[i];
    return RPC_OK;
}
