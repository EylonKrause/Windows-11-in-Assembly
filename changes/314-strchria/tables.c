/* changes/314-strchria/tables.c
 * The match relation of StrChrIA / StrRChrIA, read from StrChrIA itself.
 *
 * Both exports decide "does byte b match needle n" with CompareStringA(LOCALE_SYSTEM_DEFAULT,
 * NORM_IGNORECASE | LOCALE_USE_CP_ACP) on two one-character strings -- 60 ns a character.
 * discovery/strchria_family.c: on a single-byte ANSI code page the answer depends on (n, b) alone (not
 * on the thread locale, not on the high byte of the WORD needle), and on code page 1252 every needle
 * matches at most TWO bytes: itself and its case partner, '^' and U+02C6 (0x5E/0x88), and the NUL
 * needle the soft hyphen 0xAD. So for each needle the set is two byte values, m1 and m2 (equal when
 * the set has one member), which impl.asm compares 32 bytes at a time.
 *
 * The table is read by calling StrChrIA on every one-byte haystack for every needle: 65,280 calls,
 * ~4 ms once. init refuses the fast path -- wia_sca_fb, every call handed to the export -- on a DBCS
 * code page, when some needle matches more than two bytes, when a needle 1..255 does not match itself,
 * or without AVX2/BMI2/LZCNT.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <immintrin.h>

unsigned char wia_sca_memb[256][2];
int wia_sca_fb = 1;
void* wia_sca_fb_chr;  void* wia_sca_fb_rchr;

/* The exception handler of impl.asm's end == NULL scan, standing in for lstrlenA's __except: the
 * export's lstrlenA returns 0 for a string it cannot read to the NUL, whatever the exception, so this
 * takes every exception raised inside that procedure (not unwinds passing through it) and unwinds to
 * wia_sca_seh_resume, which returns NULL -- what StrRChrIA answers for an empty range. */
extern void wia_sca_seh_resume(void);
EXCEPTION_DISPOSITION __cdecl wia_sca_seh(EXCEPTION_RECORD* er, void* frame, CONTEXT* ctx, DISPATCHER_CONTEXT* dc) {
    if (er->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) return ExceptionContinueSearch;
    RtlUnwindEx(frame, (void*)wia_sca_seh_resume, er, NULL, ctx, dc->HistoryTable);
    return ExceptionContinueSearch;                                    /* not reached */
}

typedef char* (WINAPI *PCHR)(const char*, WORD);

static int cpu_ok(void) {
    int r[4];
    __cpuid(r, 1);
    if (!(r[2] & (1 << 27)) || !(r[2] & (1 << 28))) return 0;          /* OSXSAVE, AVX */
    if ((_xgetbv(0) & 6) != 6) return 0;
    __cpuidex(r, 7, 0);
    if (!(r[1] & (1 << 5)) || !(r[1] & (1 << 8)) || !(r[1] & (1 << 3))) return 0;   /* AVX2, BMI2, BMI1 */
    __cpuid(r, 0x80000001);
    return (r[2] & (1 << 5)) != 0;                                     /* LZCNT */
}

int wia_sca_init(void) {
    HMODULE s = LoadLibraryW(L"shlwapi.dll");
    wia_sca_fb_chr = (void*)GetProcAddress(s, "StrChrIA");
    wia_sca_fb_rchr = (void*)GetProcAddress(s, "StrRChrIA");
    if (!wia_sca_fb_chr || !wia_sca_fb_rchr) return 0;
    wia_sca_fb = 1;
    CPINFO ci;
    if (!cpu_ok() || !GetCPInfo(CP_ACP, &ci) || ci.MaxCharSize != 1) return 1;
    PCHR chr = (PCHR)wia_sca_fb_chr;
    for (int n = 0; n < 256; ++n) {
        int cnt = 0, m[2] = { -1, -1 };
        for (int b = 1; b < 256; ++b) {
            char h[2] = { (char)b, 0 };
            if (chr(h, (WORD)n)) { if (cnt < 2) m[cnt] = b; ++cnt; }
        }
        if (cnt > 2) return 1;
        if (n && (m[0] != n && m[1] != n)) return 1;
        if (cnt == 0) m[0] = 0;              /* matches nothing: comparing against the NUL is harmless, the NUL stops first */
        if (cnt < 2) m[1] = m[0];
        wia_sca_memb[n][0] = (unsigned char)m[0];
        wia_sca_memb[n][1] = (unsigned char)m[1];
    }
    wia_sca_fb = 0;
    return 1;
}
