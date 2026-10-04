/* changes/310-chartooema/tables.c
 * The two byte maps of user32's ANSI <-> OEM converters, read from the exports themselves:
 * CharToOemBuffA and OemToCharBuffA on every byte value. (Both exports are a scalar loop over a
 * 256-byte table in user32's shared data -- discovery/chartooema_contract.c.)
 *
 * The vector path copies a block unchanged when every byte maps to itself; on code pages 1252/437 that
 * is 0x00..0x7F for CharToOem and 0x00..0x7F less 0x0F, 0x14, 0x15 for OemToChar. So init records, per
 * direction, the bytes below 0x80 that do NOT map to themselves -- up to three, as 32-byte broadcast
 * vectors impl.asm compares against -- and if there are more than three, or the table maps a non-NUL
 * byte to NUL (which would change where an in-place string conversion stops), that direction hands
 * every call to the real export.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned char wia_c2oa_map[256], wia_o2ca_map[256];
__declspec(align(32)) unsigned char wia_c2oa_exc[3][32], wia_o2ca_exc[3][32];
int wia_c2oa_off, wia_o2ca_off;       /* nonzero: hand every call to the export */
void* wia_c2oa_fb_b; void* wia_c2oa_fb_s; void* wia_o2ca_fb_b; void* wia_o2ca_fb_s;

static int prep(void* fb, unsigned char* map, unsigned char exc[3][32]) {
    for (unsigned b = 0; b < 256; ++b) {
        char c = (char)b, o[2] = { 0, 0 };
        ((BOOL (WINAPI*)(LPCSTR, LPSTR, DWORD))fb)(&c, o, 1);
        map[b] = (unsigned char)o[0];
    }
    if (map[0] != 0) return 1;
    for (unsigned b = 1; b < 256; ++b) if (map[b] == 0) return 1;
    int k = 0;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 32; ++j) exc[i][j] = 0x80;   /* never equal to a byte < 0x80 */
    for (unsigned b = 0; b < 0x80; ++b)
        if (map[b] != b) { if (k == 3) return 1; for (int j = 0; j < 32; ++j) exc[k][j] = (unsigned char)b; ++k; }
    return 0;
}

int wia_c2oa_init(void) {
    HMODULE u = LoadLibraryW(L"user32.dll");
    wia_c2oa_fb_b = (void*)GetProcAddress(u, "CharToOemBuffA"); wia_c2oa_fb_s = (void*)GetProcAddress(u, "CharToOemA");
    wia_o2ca_fb_b = (void*)GetProcAddress(u, "OemToCharBuffA"); wia_o2ca_fb_s = (void*)GetProcAddress(u, "OemToCharA");
    if (!wia_c2oa_fb_b || !wia_c2oa_fb_s || !wia_o2ca_fb_b || !wia_o2ca_fb_s) return 0;
    wia_c2oa_off = prep(wia_c2oa_fb_b, wia_c2oa_map, wia_c2oa_exc);
    wia_o2ca_off = prep(wia_o2ca_fb_b, wia_o2ca_map, wia_o2ca_exc);
    return 1;
}
