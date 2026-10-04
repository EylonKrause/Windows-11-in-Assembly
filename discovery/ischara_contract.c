/* discovery/ischara_contract.c
   user32's 8-bit character classifiers -- IsCharAlphaA, IsCharAlphaNumericA, IsCharUpperA,
   IsCharLowerA (bodies in kernelbase) -- before they are reimplemented.

   WHY. discovery/ischar_family.c found the WIDE forms already cost 2.0-2.5 ns: nothing to win. The
   8-bit forms do more: kernelbase's IsCharAlphaA (RVA 0xBA320) converts its byte with
   RtlMultiByteToUnicodeN, walks the three-level CT_CTYPE1 table, and then asks whether the ANSI code
   page is DBCS. On a single-byte code page the answer for a byte is fixed, so the whole function is a
   256-entry table -- if that is all it is. Established here:

     1. the exact return value for every byte, all four functions (0/1, or the class bit?);
     2. only the low byte matters? (the argument is a CHAR in cl; garbage above it must be ignored);
     3. thread locale and thread UI language: tr-TR, ja-JP, ar-SA, ru-RU change nothing?
     4. the cost per call.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef BOOL (WINAPI *PF)(CHAR);
typedef BOOL (WINAPI *PFQ)(uintptr_t);       /* the same export, called with junk above the byte */

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"user32.dll");
    const char* nm[4] = { "IsCharAlphaA", "IsCharAlphaNumericA", "IsCharUpperA", "IsCharLowerA" };
    PF f[4]; for (int k = 0; k < 4; ++k) f[k] = (PF)GetProcAddress(u, nm[k]);
    CPINFO ci; GetCPInfo(CP_ACP, &ci);
    printf("ACP %u, MaxCharSize %u\n", GetACP(), ci.MaxCharSize);
    static int base[4][256];
    for (int k = 0; k < 4; ++k) {
        int vals[4] = { 0 }, other = 0, cnt = 0;
        for (int b = 0; b < 256; ++b) {
            BOOL r = f[k]((CHAR)b); base[k][b] = r;
            if (r == 0) vals[0]++; else if (r == 1) { vals[1]++; ++cnt; } else ++other;
        }
        int junk = 0;
        for (int b = 0; b < 256; ++b) {
            uintptr_t arg = 0xDEADBEEF12345600ull | (unsigned)b;
            if (((PFQ)f[k])(arg) != base[k][b]) ++junk;
        }
        printf("\n%-20s TRUE for %3d bytes, values 0:%d 1:%d other:%d; junk above the byte changes %d answers\n  TRUE: ", nm[k], cnt, vals[0], vals[1], other, junk);
        for (int b = 0; b < 256; ++b) if (base[k][b]) printf(b >= 0x21 && b < 0x7F ? "%c" : "<%02X>", b);
        printf("\n");
    }
    LCID loc[4] = { 0x041F, 0x0411, 0x0401, 0x0419 };
    LCID old = GetThreadLocale(); LANGID oldui = GetThreadUILanguage();
    for (int l = 0; l < 4; ++l) {
        SetThreadLocale(loc[l]); SetThreadUILanguage(LANGIDFROMLCID(loc[l]));
        int d = 0; for (int k = 0; k < 4; ++k) for (int b = 0; b < 256; ++b) if (f[k]((CHAR)b) != base[k][b]) ++d;
        printf("thread locale/UI %04X: %d answers differ\n", loc[l], d);
    }
    SetThreadLocale(old); SetThreadUILanguage(oldui);
    return 0;
}
