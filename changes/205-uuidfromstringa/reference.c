// changes/205-uuidfromstringa/reference.c
// The correctness oracle for rpcrt4!UuidFromStringA. Not fast; just obviously right.
//
// Contract, every line of it measured against the live export in probes/ufs.c:
//   * exactly 36 characters, UNBRACED: 8 hex, '-', 4 hex, '-', 4 hex, '-', 4 hex, '-', 12 hex, then
//     a NUL at [36]. Hex digits are case-insensitive;
//   * a BRACED string is REJECTED with 1705 (RPC_S_INVALID_STRING_UUID). Note this is the OPPOSITE
//     of change 118's ntdll!RtlGUIDFromString, which *requires* the braces -- the two contracts are
//     not interchangeable, which is exactly why this one was probed rather than assumed;
//   * StringUuid == NULL is SUCCESS: return 0 and write the nil UUID (16 zero bytes);
//   * every other malformed input -> 1705, and the output GUID is NOT TOUCHED. A pre-poisoned GUID
//     came back byte-identical from all eleven malformed shapes tried, including one short, one
//     long, bad hex at either end, wrong separators, spaces for separators, leading and trailing
//     whitespace, and a valid 36 characters followed by junk instead of a terminator.
//
// Byte order: Data1/Data2/Data3 are little-endian integers, Data4 is in memory order, so
// "deadbeef-1234-5678-9abc-def011223344" becomes the bytes
//      EF BE AD DE  34 12  78 56  9A BC DE F0 11 22 33 44
// which is the same 3,2,1,0, 5,4, 7,6, 8..15 permutation the formatters in changes 202/203 apply in
// the other direction.
#include <windows.h>

#define RPC_OK       0
#define RPC_BAD_UUID 1705

static int hexval(unsigned char c, unsigned* out){
    if (c >= '0' && c <= '9') { *out = c - '0';        return 1; }
    if (c >= 'a' && c <= 'f') { *out = c - 'a' + 10;   return 1; }
    if (c >= 'A' && c <= 'F') { *out = c - 'A' + 10;   return 1; }
    return 0;
}

long ref_uuidfromstringa(unsigned char* s, GUID* out)
{
    unsigned char b[16];
    /* char offsets of the two hex digits making up each GUID byte */
    static const int PAIR[16][2] = {
        {6,7}, {4,5}, {2,3}, {0,1},
        {11,12}, {9,10},
        {16,17}, {14,15},
        {19,20}, {21,22},
        {24,25}, {26,27}, {28,29}, {30,31}, {32,33}, {34,35}
    };

    if (s == 0) {                       /* a null pointer means the nil uuid, and it succeeds */
        int i; for (i = 0; i < 16; ++i) ((unsigned char*)out)[i] = 0;
        return RPC_OK;
    }

    /* the string must be exactly 36 characters long -- walked, so a shorter one is never read past */
    {
        int n = 0;
        while (n < 37 && s[n]) ++n;
        if (n != 36) return RPC_BAD_UUID;
    }

    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return RPC_BAD_UUID;

    {
        int i;
        for (i = 0; i < 16; ++i) {
            unsigned hi, lo;
            if (!hexval(s[PAIR[i][0]], &hi)) return RPC_BAD_UUID;
            if (!hexval(s[PAIR[i][1]], &lo)) return RPC_BAD_UUID;
            b[i] = (unsigned char)((hi << 4) | lo);
        }
    }

    { int i; for (i = 0; i < 16; ++i) ((unsigned char*)out)[i] = b[i]; }
    return RPC_OK;
}
