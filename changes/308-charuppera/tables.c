/* changes/308-charuppera/tables.c
 * The two byte maps, built from the export under test: CharUpperA / CharLowerA in CHARACTER mode, one
 * byte value at a time -- kernelbase's own MultiByteToWideChar -> LCMapString -> WideCharToMultiByte
 * round trip, not a transcription of code page 1252.
 *
 * A byte table is the whole function only on a SINGLE-BYTE ANSI code page: on a DBCS one, lead bytes
 * pair with what follows. wia_cua_init() therefore installs the real exports as a fallback and, when
 * GetCPInfo reports MaxCharSize != 1, sets wia_cua_dbcs so that every call is handed to them -- the
 * dispatch boundary, stated in the code rather than assumed. It also refuses to report success unless
 * the maps below 0x80 are the ASCII range rule the vector path relies on.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

unsigned char wia_cua_up[256], wia_cua_dn[256];
int wia_cua_dbcs;
void* wia_cua_fb_upbuff; void* wia_cua_fb_dnbuff; void* wia_cua_fb_up; void* wia_cua_fb_dn;

int wia_cua_init(void) {
    HMODULE u = LoadLibraryW(L"user32.dll");
    wia_cua_fb_upbuff = (void*)GetProcAddress(u, "CharUpperBuffA");
    wia_cua_fb_dnbuff = (void*)GetProcAddress(u, "CharLowerBuffA");
    wia_cua_fb_up = (void*)GetProcAddress(u, "CharUpperA");
    wia_cua_fb_dn = (void*)GetProcAddress(u, "CharLowerA");
    if (!wia_cua_fb_upbuff || !wia_cua_fb_dnbuff || !wia_cua_fb_up || !wia_cua_fb_dn) return 0;
    CPINFO ci;
    wia_cua_dbcs = !GetCPInfo(CP_ACP, &ci) || ci.MaxCharSize != 1;
    for (unsigned b = 0; b < 256; ++b) {
        wia_cua_up[b] = (unsigned char)(uintptr_t)((LPSTR (WINAPI*)(LPSTR))wia_cua_fb_up)((LPSTR)(uintptr_t)b);
        wia_cua_dn[b] = (unsigned char)(uintptr_t)((LPSTR (WINAPI*)(LPSTR))wia_cua_fb_dn)((LPSTR)(uintptr_t)b);
    }
    wia_cua_up[0] = wia_cua_dn[0] = 0;          /* char mode cannot be asked about 0 (it is NULL) */
    for (unsigned b = 1; b < 0x80; ++b) {
        if (wia_cua_up[b] != ((b >= 'a' && b <= 'z') ? b - 32 : b)) return 0;
        if (wia_cua_dn[b] != ((b >= 'A' && b <= 'Z') ? b + 32 : b)) return 0;
    }
    return 1;
}
