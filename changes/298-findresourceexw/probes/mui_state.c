/* probes/mui_state.c -- the live export is NOT a pure function of its arguments.
 * For a language that is not in the module, the FIRST call reports a different
 * Win32 error from every call after it, because ntdll caches the failed attempt
 * to load an alternate (MUI) resource module for that image.
 * This is why correctness.c warms each tuple before comparing.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef HRSRC (WINAPI *pfn)(HMODULE, LPCWSTR, LPCWSTR, WORD);
int main(void)
{
    pfn F = (pfn)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FindResourceExW");
    HMODULE u = LoadLibraryW(L"user32.dll");
    WORD langs[] = { 0x0809, 0x0C0C, 0x040B, 0x0101, 0x7777 };
    for (int i = 0; i < 5; ++i) {
        printf("lang 0x%04X: ", langs[i]);
        for (int k = 0; k < 5; ++k) {
            SetLastError(0xABCD);
            HRSRC r = F(u, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), langs[i]);
            printf("%s/%-6lu ", r ? "HR" : "NL", GetLastError());
        }
        printf("\n");
    }
    /* and a fresh module, to show it is per-image state */
    HMODULE c1 = LoadLibraryExW(L"comctl32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    printf("comctl32 fresh map, lang 0x0809: ");
    for (int k = 0; k < 4; ++k) {
        SetLastError(0xABCD);
        HRSRC r = F(c1, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0x0809);
        printf("%s/%-6lu ", r ? "HR" : "NL", GetLastError());
    }
    printf("\n");
    return 0;
}
