/* changes/312-ischaralphaa/tables.c
 * The four answers for every byte value, read from the exports themselves: IsCharAlphaA,
 * IsCharAlphaNumericA, IsCharUpperA and IsCharLowerA on bytes 0..255. discovery/ischara_contract.c:
 * every answer is 0 or 1, only the low byte of the argument matters, and the thread locale and UI
 * language change nothing -- on a SINGLE-BYTE ANSI code page. On a DBCS one, kernelbase consults the
 * lead-byte table as well; wia_ica_dbcs is then set and every call is handed to the export.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned char wia_ica_alpha[256], wia_ica_alnum[256], wia_ica_upper[256], wia_ica_lower[256];
int wia_ica_dbcs;
void* wia_ica_fb_alpha; void* wia_ica_fb_alnum; void* wia_ica_fb_upper; void* wia_ica_fb_lower;

int wia_ica_init(void) {
    HMODULE u = LoadLibraryW(L"user32.dll");
    wia_ica_fb_alpha = (void*)GetProcAddress(u, "IsCharAlphaA");
    wia_ica_fb_alnum = (void*)GetProcAddress(u, "IsCharAlphaNumericA");
    wia_ica_fb_upper = (void*)GetProcAddress(u, "IsCharUpperA");
    wia_ica_fb_lower = (void*)GetProcAddress(u, "IsCharLowerA");
    if (!wia_ica_fb_alpha || !wia_ica_fb_alnum || !wia_ica_fb_upper || !wia_ica_fb_lower) return 0;
    CPINFO ci;
    wia_ica_dbcs = !GetCPInfo(CP_ACP, &ci) || ci.MaxCharSize != 1;
    typedef BOOL (WINAPI *PF)(CHAR);
    for (int b = 0; b < 256; ++b) {
        BOOL a = ((PF)wia_ica_fb_alpha)((CHAR)b), n = ((PF)wia_ica_fb_alnum)((CHAR)b);
        BOOL up = ((PF)wia_ica_fb_upper)((CHAR)b), lo = ((PF)wia_ica_fb_lower)((CHAR)b);
        if ((a | n | up | lo) & ~1) return 0;          /* the tables hold 0/1 only */
        wia_ica_alpha[b] = (unsigned char)a; wia_ica_alnum[b] = (unsigned char)n;
        wia_ica_upper[b] = (unsigned char)up; wia_ica_lower[b] = (unsigned char)lo;
    }
    return 1;
}
