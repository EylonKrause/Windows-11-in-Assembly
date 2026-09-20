// changes/206-stringfromguid2/reference.c
// The correctness oracle for combase!StringFromGUID2. Not fast; just obviously right.
//
// Contract, measured against the live export in probes/sfg.c:
//   * cchMax >= 39 -> write "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" in UPPER-case hex plus a NUL,
//     and return 39, the character count INCLUDING the terminator;
//   * cchMax <= 38 -> return 0 and leave the buffer COMPLETELY UNTOUCHED. There is no truncating
//     path, which is the one place this differs sharply from ConvertGuidToStringW (change 202),
//     where 1..38 writes a truncated prefix;
//   * cchMax is SIGNED, -1 and -1000 both return 0;
//   * the length is checked before the buffer is used, so a NULL buffer with cchMax 0 returns 0
//     without faulting.
//
// The characters are identical to change 202's ConvertGuidToStringW, not merely the same shape:
// 200 000 random GUIDs rendered through both live exports differed in 0 characters. Field order note:
// Data1/Data2/Data3 are little-endian integers printed most-significant first, so the 16 GUID bytes
// appear in print order 3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15.
#include <windows.h>

#define WIA_GUID_CELLS 39   /* 38 characters + the terminator */

int ref_StringFromGUID2(const GUID* guid, wchar_t* str, int cchMax)
{
    static const char HEX[] = "0123456789ABCDEF";
    static const int ORDER[16] = { 3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15 };
    const unsigned char* g = (const unsigned char*)guid;
    wchar_t full[WIA_GUID_CELLS];
    unsigned k = 0;
    int i;

    if (cchMax < WIA_GUID_CELLS) return 0;      /* signed compare, buffer untouched */

    full[k++] = L'{';
    for (i = 0; i < 16; i++) {
        unsigned char b = g[ORDER[i]];
        full[k++] = (wchar_t)HEX[b >> 4];
        full[k++] = (wchar_t)HEX[b & 15];
        if (i == 3 || i == 5 || i == 7 || i == 9) full[k++] = L'-';
    }
    full[k++] = L'}';
    full[k]   = 0;                              /* k == 38 here */

    for (i = 0; i < WIA_GUID_CELLS; i++) str[i] = full[i];
    return WIA_GUID_CELLS;
}
