/* changes/313-strtointexa/tables.c
 * Not a table this time -- the dispatch boundary. impl.asm parses the bytes in place, which is only
 * the export's answer when converting them to UTF-16 cannot change what StrToInt64ExW sees: a
 * single-byte ANSI code page on which every byte converts to exactly one unit, bytes below 0x80 to
 * themselves and bytes above to units above. Checked here byte by byte, together with the AVX2/BMI2
 * the NUL scan uses. Otherwise wia_sti_fb is set and every call is handed to the export.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <immintrin.h>

int wia_sti_fb = 1;
unsigned long long wia_sti_maxlen = 0x7FFFFFFEull;   /* the export's (int)(len + 1) stays a length below this */
void* wia_sti_fb64; void* wia_sti_fb32;

static int cpu_ok(void) {
    int r[4];
    __cpuid(r, 1);
    if (!(r[2] & (1 << 27)) || !(r[2] & (1 << 28))) return 0;          /* OSXSAVE, AVX */
    if ((_xgetbv(0) & 6) != 6) return 0;                              /* XMM and YMM state enabled */
    __cpuidex(r, 7, 0);
    return (r[1] & (1 << 5)) && (r[1] & (1 << 8));                    /* AVX2, BMI2 */
}

static int acp_ok(void) {
    CPINFO ci;
    if (!GetCPInfo(CP_ACP, &ci) || ci.MaxCharSize != 1) return 0;
    for (int b = 1; b < 256; ++b) {
        char c = (char)b; wchar_t w[2] = { 0, 0 };
        if (MultiByteToWideChar(CP_ACP, 0, &c, 1, w, 2) != 1) return 0;
        if (b < 0x80 ? w[0] != (wchar_t)b : w[0] < 0x80) return 0;
    }
    return 1;
}

int wia_sti_init(void) {
    HMODULE s = LoadLibraryW(L"shlwapi.dll");
    wia_sti_fb64 = (void*)GetProcAddress(s, "StrToInt64ExA");
    wia_sti_fb32 = (void*)GetProcAddress(s, "StrToIntExA");
    if (!wia_sti_fb64 || !wia_sti_fb32) return 0;
    wia_sti_fb = !(cpu_ok() && acp_ok());
    return 1;
}
