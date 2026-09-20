// changes/203-convertguidtostringa/reference.c
// The correctness oracle for iphlpapi!ConvertGuidToStringA. Not fast; just correct.
//
// Why this function is slow, which is the whole reason it is here: it does not format the GUID.
// The disassembly spills the eleven GUID fields to the stack as varargs, loads the literal format
// string "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}" and calls a StringCchPrintfW clone
// that re-parses that format on every call and dispatches each conversion through a per-character
// output helper. 315 ns to write 38 characters.
//
// Contract, every line of it measured against the live export in probes/cgs.c:
//   * output is "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}", UPPER-case hex, 38 chars + NUL,
//     so it needs 39 cells;
//   * Guid == NULL or String == NULL -> 87 (ERROR_INVALID_PARAMETER), buffer untouched;
//   * StringLenInChars == 0          -> 122 (ERROR_INSUFFICIENT_BUFFER), buffer UNTOUCHED;
//   * 1 <= cch <= 38                 -> 122, and the buffer IS written: the first cch-1 characters
//     followed by a NUL at [cch-1]. cch = 38 stops one short of the closing brace;
//   * cch >= 39                      -> 0, the full string;
//   * cch >= 0x80000000              -> 122 with String[0] = 0, NOT 87. The inner helper rejects
//     (cch-1) > 0x7FFFFFFE and the wrapper maps its E_INVALIDARG to 122 like any other failure;
//   * an unaligned GUID pointer is fine;
//   * the WIDE form (change 202) produces the same characters, 0 differences over 200 000 random
//     (GUID, cch) pairs compared character for character, return value included.
//
// Field order note: Data1/Data2/Data3 are little-endian integers printed most-significant first,
// so the 16 GUID bytes appear in print order 3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15. That single
// permutation is what lets the assembly do the whole conversion with one vpshufb.
#include <windows.h>

#define WIA_GUID_CHARS 38u   /* not counting the terminator */

int ref_ConvertGuidToStringA(const GUID* guid, char* str, unsigned long cch){
    if(guid == 0 || str == 0) return 87;
    if(cch == 0) return 122;

    static const char HEX[] = "0123456789ABCDEF";
    const unsigned char* g = (const unsigned char*)guid;
    static const int ORDER[16] = { 3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15 };

    char full[WIA_GUID_CHARS + 1];
    unsigned k = 0;
    full[k++] = '{';
    for(int i = 0; i < 16; i++){
        unsigned char b = g[ORDER[i]];
        full[k++] = (char)HEX[b >> 4];
        full[k++] = (char)HEX[b & 15];
        if(i == 3 || i == 5 || i == 7 || i == 9) full[k++] = '-';
    }
    full[k++] = '}';
    full[k]   = 0;                         /* k == WIA_GUID_CHARS here */

    if(cch >= 0x80000000ul){ str[0] = 0; return 122; }
    if(cch >= WIA_GUID_CHARS + 1u){
        for(unsigned i = 0; i <= WIA_GUID_CHARS; i++) str[i] = full[i];
        return 0;
    }
    /* truncate: cch-1 characters then a terminator */
    for(unsigned i = 0; i + 1 < cch; i++) str[i] = full[i];
    str[cch - 1] = 0;
    return 122;
}
