/* changes/287-getstringtypew/reference.c
 *
 * THE SCALAR MODEL for GetStringTypeW, from the contract measured in probes/contract.c:
 *
 *     BOOL GetStringTypeW(DWORD dwInfoType, LPCWCH lpSrcStr, int cchSrc, LPWORD lpCharType)
 *
 *   * exactly ONE info type at a time: CT_CTYPE1 (1), CT_CTYPE2 (2) or CT_CTYPE3 (4). CT_CTYPE1|CT_CTYPE2
 *     is refused, and so are 0 and 8;
 *   * the class of a code unit is a pure, context-free, locale-invariant function of that code unit --
 *     20000 random strings and seven thread locales said so, which is the only reason a table is legal;
 *   * cchSrc > 0 is a COUNT of code units, and exactly that many words are written;
 *   * cchSrc == -1 means NUL-terminated AND INCLUDES THE TERMINATOR: for a three-character string a
 *     fourth word is written, the class of U+0000;
 *   * cchSrc == 0 is refused, and so are a NULL source and a NULL destination;
 *   * an embedded NUL under an explicit count is just another code unit, classified like any other.
 *
 * This model reads the class straight out of the flat extraction that tables.c builds, so it shares no
 * structure with impl.asm -- which goes through the two-level directory and page tables, and has a
 * separate Latin-1 path. A mistake in either level of that indirection shows up here as a disagreement
 * rather than being reproduced on both sides.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int wia_gst_init(void);
extern unsigned char  wia_gst_dir[3][256];
extern unsigned short wia_gst_page[3][96][256];

static int type_index(DWORD kind)
{
    if (kind == 1) return 0;
    if (kind == 2) return 1;
    if (kind == 4) return 2;
    return -1;
}

int ref_getstringtypew(DWORD kind, const wchar_t* src, int cch, unsigned short* out)
{
    int t = type_index(kind);
    int i, n;

    if (t < 0) return 0;
    if (!src || !out) return 0;
    if (cch == 0) return 0;

    if (cch < 0) {
        n = 0;
        while (src[n]) ++n;
        ++n;                                  /* -1 INCLUDES the terminator */
    } else {
        n = cch;
    }

    for (i = 0; i < n; ++i) {
        unsigned c = (unsigned short)src[i];
        out[i] = wia_gst_page[t][wia_gst_dir[t][c >> 8]][c & 0xFF];
    }
    return 1;
}
