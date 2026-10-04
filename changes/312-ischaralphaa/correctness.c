/* changes/312-ischaralphaa/correctness.c
 * Gate 1: the four classifiers against live user32 and the oracle -- every byte value, with junk in
 * every bit above the byte (the argument is a CHAR; the register's upper bits are not the caller's
 * promise), and under four thread locales.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef BOOL (WINAPI *PF)(CHAR);
typedef BOOL (WINAPI *PFQ)(uintptr_t);
extern BOOL wia_ischaralphaa(CHAR), wia_ischaralphanumerica(CHAR), wia_ischaruppera(CHAR), wia_ischarlowera(CHAR);
BOOL ref_ischaralphaa(CHAR), ref_ischaralphanumerica(CHAR), ref_ischaruppera(CHAR), ref_ischarlowera(CHAR);
int wia_ica_init(void);
extern int wia_ica_dbcs;

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (!wia_ica_init()) { printf("init failed\n"); return 3; }
    if (wia_ica_dbcs) printf("note: DBCS code page -- every call is handed to the export\n");
    HMODULE u = LoadLibraryW(L"user32.dll");
    const char* nm[4] = { "IsCharAlphaA", "IsCharAlphaNumericA", "IsCharUpperA", "IsCharLowerA" };
    PF s[4], o[4] = { wia_ischaralphaa, wia_ischaralphanumerica, wia_ischaruppera, wia_ischarlowera };
    PF r[4] = { ref_ischaralphaa, ref_ischaralphanumerica, ref_ischaruppera, ref_ischarlowera };
    for (int k = 0; k < 4; ++k) s[k] = (PF)GetProcAddress(u, nm[k]);
    long long tested = 0, fails = 0;
    static const uintptr_t junk[] = { 0, 0xFF00, 0xDEADBEEF00ull, 0xFFFFFFFFFFFFFF00ull, 0x123456789ABCDE00ull };
    LCID loc[5] = { 0, 0x041F, 0x0411, 0x0401, 0x0419 };
    LCID old = GetThreadLocale();
    for (int l = 0; l < 5; ++l) {
        if (loc[l]) SetThreadLocale(loc[l]);
        for (int k = 0; k < 4; ++k)
            for (int b = 0; b < 256; ++b)
                for (int j = 0; j < 5; ++j) {
                    uintptr_t arg = junk[j] | (unsigned)b;
                    BOOL a = ((PFQ)s[k])(arg), c = ((PFQ)o[k])(arg), d = r[k]((CHAR)b);
                    ++tested;
                    if (a != c || a != d) { if (fails < 15) printf("FAIL %s byte %02X junk %llX locale %04X: sys %d ours %d ref %d\n", nm[k], b, (unsigned long long)junk[j], loc[l], a, c, d); ++fails; }
                }
    }
    SetThreadLocale(old);
    /* The hand-off taken on a DBCS code page, forced: every call must come back as the export's answer,
       with the argument register passed through untouched. */
    long long handed = 0;
    int was = wia_ica_dbcs; wia_ica_dbcs = 1;
    for (int k = 0; k < 4; ++k)
        for (int b = 0; b < 256; ++b)
            for (int j = 0; j < 5; ++j) {
                uintptr_t arg = junk[j] | (unsigned)b;
                BOOL a = ((PFQ)s[k])(arg), c = ((PFQ)o[k])(arg);
                ++tested; ++handed;
                if (a != c) { if (fails < 15) printf("FAIL (hand-off) %s byte %02X junk %llX: sys %d ours %d\n", nm[k], b, (unsigned long long)junk[j], a, c); ++fails; }
            }
    wia_ica_dbcs = was;
    if (!fails) printf("CORRECTNESS: PASS (IsCharAlphaA/IsCharAlphaNumericA/IsCharUpperA/IsCharLowerA vs live user32 + oracle, %lld cases: every byte x five upper-bit patterns x five thread locales, and %lld through the forced DBCS hand-off)\n", tested, handed);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
