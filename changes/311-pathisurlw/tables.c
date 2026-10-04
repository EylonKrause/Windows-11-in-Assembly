/* changes/311-pathisurlw/tables.c
 * The set of units PathIsURLW / PathIsURLA accept in a URL scheme, read from the exports: a unit c is
 * accepted when "ab" c "x:" is a URL (discovery/pathisurl_contract.c: + - . 0-9 A-Z a-z, nothing else,
 * nothing above 0x7F, the same at every index). impl.asm tests that set with ranges; init confirms the
 * exports still agree, and if they do not, wia_piu_off hands every call to them.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned char wia_piu_ok[128];
int wia_piu_off;
void* wia_piu_fb_w; void* wia_piu_fb_a;

static int in_set(unsigned c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

int wia_piu_init(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    wia_piu_fb_w = (void*)GetProcAddress(h, "PathIsURLW");
    wia_piu_fb_a = (void*)GetProcAddress(h, "PathIsURLA");
    if (!wia_piu_fb_w || !wia_piu_fb_a) return 0;
    BOOL (WINAPI* fw)(LPCWSTR) = (BOOL (WINAPI*)(LPCWSTR))wia_piu_fb_w;
    BOOL (WINAPI* fa)(LPCSTR) = (BOOL (WINAPI*)(LPCSTR))wia_piu_fb_a;
    wia_piu_off = 0;
    for (unsigned c = 1; c < 256; ++c) {
        if (c == ':') continue;
        wchar_t w[6] = { 'a', 'b', (wchar_t)c, 'x', ':', 0 };
        char a[6] = { 'a', 'b', (char)c, 'x', ':', 0 };
        int ew = fw(w) != 0, ea = fa(a) != 0, want = c < 128 && in_set(c);
        if (ew != want || ea != want) wia_piu_off = 1;
        if (c < 128) wia_piu_ok[c] = (unsigned char)want;
    }
    return 1;
}
