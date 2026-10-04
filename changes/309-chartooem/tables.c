/* changes/309-chartooem/tables.c
 * The two maps, built from the exports under test: CharToOemBuffW on every UTF-16 unit, one at a time
 * (WideCharToMultiByte(CP_OEMCP) with user32's default character, '_'), and OemToCharBuffW on every
 * byte (MultiByteToWideChar(CP_OEMCP, MB_PRECOMPOSED | MB_USEGLYPHCHARS)). Neither is change 028's or
 * 029's ntdll table: discovery/chartooem_contract.c found the first differs from a plain
 * WideCharToMultiByte on 64,797 units (the default character) and the second on 32 bytes (the glyphs).
 *
 * A per-unit / per-byte table is the whole function only on a SINGLE-BYTE OEM code page; on a DBCS one
 * wia_c2o_dbcs is set and every call is handed to the real export. Init also checks the two facts the
 * vector paths rely on: units below 0x80 map to themselves, and bytes 0x20..0x7E map to themselves.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned char wia_c2o_map[65536 + 4];      /* + padding: the gathers read 4 bytes at each index */
unsigned short wia_o2c_map[256 + 2];
int wia_c2o_dbcs;
void* wia_c2o_fb_c2ob; void* wia_c2o_fb_c2o; void* wia_c2o_fb_o2cb; void* wia_c2o_fb_o2c;

int wia_c2o_init(void) {
    HMODULE u = LoadLibraryW(L"user32.dll");
    wia_c2o_fb_c2ob = (void*)GetProcAddress(u, "CharToOemBuffW");
    wia_c2o_fb_c2o = (void*)GetProcAddress(u, "CharToOemW");
    wia_c2o_fb_o2cb = (void*)GetProcAddress(u, "OemToCharBuffW");
    wia_c2o_fb_o2c = (void*)GetProcAddress(u, "OemToCharW");
    if (!wia_c2o_fb_c2ob || !wia_c2o_fb_c2o || !wia_c2o_fb_o2cb || !wia_c2o_fb_o2c) return 0;
    CPINFO ci;
    wia_c2o_dbcs = !GetCPInfo(CP_OEMCP, &ci) || ci.MaxCharSize != 1;
    for (unsigned c = 0; c < 65536; ++c) {
        wchar_t w = (wchar_t)c; char o[2] = { 0, 0 };
        ((BOOL (WINAPI*)(LPCWSTR, LPSTR, DWORD))wia_c2o_fb_c2ob)(&w, o, 1);
        wia_c2o_map[c] = (unsigned char)o[0];
    }
    for (unsigned b = 0; b < 256; ++b) {
        char c = (char)b; wchar_t w[2] = { 0, 0 };
        ((BOOL (WINAPI*)(LPCSTR, LPWSTR, DWORD))wia_c2o_fb_o2cb)(&c, w, 1);
        wia_o2c_map[b] = w[0];
    }
    for (unsigned c = 0; c < 0x80; ++c) if (wia_c2o_map[c] != c) return 0;
    for (unsigned b = 0x20; b < 0x7F; ++b) if (wia_o2c_map[b] != b) return 0;
    return 1;
}
