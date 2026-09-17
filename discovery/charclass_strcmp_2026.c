/* discovery/charclass_strcmp_2026.c
 *
 * ROUND TWO OF THE COVERAGE SWEEP: the families ntdll does not own.
 *
 * discovery/ntdll_uncovered_2026.c found ntdll's remaining Rtl* surface largely exhausted -- the
 * bitmap writers already run at memory bandwidth (8 KB in 54 ns), the size calculators are O(1) and
 * never scan at all, and the memory primitives are forwarders to the optimised CRT. One outlier:
 * RtlUpperChar at 4.98 ns against RtlUpcaseUnicodeChar at 0.98.
 *
 * That outlier is the clue this file follows. A SINGLE-CHARACTER OPERATION THAT COSTS FIVE
 * NANOSECONDS is not doing a table lookup; it is going somewhere -- a locale, a code page, a
 * conversion. So: time every single-character classifier and converter Windows exports, and the
 * ordinal string comparisons in shlwapi, and see which ones are paying for a journey.
 *
 * A number here is only a REASON TO LOOK. Changes 005, 006 and 129 all parked against incumbents
 * that were already lean, and changes 274 and 276 parked because the entire measured cost was an OS
 * call this project does not own. A slow-looking function may be slow for a reason that cannot be
 * removed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>

static LARGE_INTEGER fq;
static HMODULE hkb, hsh, huc, hu32;

typedef BOOL  (WINAPI *F_isw)(WCHAR);
typedef BOOL  (WINAPI *F_isa)(CHAR);
typedef int   (WINAPI *F_cmpw)(PCWSTR, PCWSTR);
typedef int   (WINAPI *F_cmpnw)(PCWSTR, PCWSTR, int);
typedef PCWSTR(WINAPI *F_chrw)(PCWSTR, WCHAR);
typedef int   (__cdecl *F_tou)(int);

static void* R(HMODULE m, const char* n, const char* tag)
{
    void* p = m ? (void*)GetProcAddress(m, n) : 0;
    if (!p) printf("    %-40s (not exported by %s)\n", n, tag);
    return p;
}

#define TIME(label, reps, stmt)                                                   \
    do {                                                                          \
        LARGE_INTEGER t0, t1; long i_; volatile long long sink_ = 0;               \
        for (i_ = 0; i_ < (reps) / 8; ++i_) { sink_ += (long long)(stmt); }        \
        QueryPerformanceCounter(&t0);                                             \
        for (i_ = 0; i_ < (reps); ++i_) { sink_ += (long long)(stmt); }            \
        QueryPerformanceCounter(&t1);                                             \
        printf("    %-46s %9.2f ns\n", label,                                     \
               (double)(t1.QuadPart - t0.QuadPart) * 1e9 / fq.QuadPart / (reps)); \
    } while (0)

int main(void)
{
    static wchar_t A[512], B[512], C[512];
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&fq);
    hkb  = LoadLibraryW(L"kernelbase.dll");
    hsh  = LoadLibraryW(L"shlwapi.dll");
    huc  = LoadLibraryW(L"ucrtbase.dll");
    hu32 = LoadLibraryW(L"user32.dll");

    for (i = 0; i < 511; ++i) { A[i] = (wchar_t)(L'a' + (i % 26)); B[i] = A[i]; C[i] = A[i]; }
    A[511] = B[511] = C[511] = 0;
    C[400] = L'Z';                          /* differs late, so the compare has to walk */

    printf("== SINGLE-CHARACTER CLASSIFIERS (kernelbase / user32) ==\n");
    printf("   RtlUpcaseUnicodeChar costs 0.98 ns; anything far above that is going somewhere\n");
    {
        static const char* NAMES[] = {
            "IsCharAlphaW", "IsCharAlphaNumericW", "IsCharLowerW", "IsCharUpperW",
            "IsCharSpaceW", "IsCharDigitW", "IsCharXDigitW", "IsCharPunctW",
            "IsCharCntrlW", "IsCharBlankW" };
        unsigned k;
        for (k = 0; k < sizeof NAMES / sizeof NAMES[0]; ++k) {
            F_isw f = (F_isw)R(hkb, NAMES[k], "kernelbase");
            if (!f) f = (F_isw)R(hu32, NAMES[k], "user32");
            if (f) TIME(NAMES[k], 3000000, f(L'q'));
        }
        {
            F_isa g = (F_isa)R(hkb, "IsCharAlphaA", "kernelbase");
            if (!g) g = (F_isa)R(hu32, "IsCharAlphaA", "user32");
            if (g) TIME("IsCharAlphaA", 3000000, g('q'));
        }
    }

    printf("\n== the CRT's own single-character converters, for scale ==\n");
    {
        F_tou tu = (F_tou)R(huc, "toupper", "ucrtbase");
        F_tou tl = (F_tou)R(huc, "tolower", "ucrtbase");
        F_tou _tu = (F_tou)R(huc, "_toupper", "ucrtbase");
        F_tou twu = (F_tou)R(huc, "towupper", "ucrtbase");
        if (tu)  TIME("ucrtbase toupper",  3000000, tu('q'));
        if (tl)  TIME("ucrtbase tolower",  3000000, tl('Q'));
        if (_tu) TIME("ucrtbase _toupper", 3000000, _tu('q'));
        if (twu) TIME("ucrtbase towupper", 3000000, twu(L'q'));
    }

    printf("\n== ORDINAL STRING COMPARISON (shlwapi) -- 511 wchar, differing at 400 ==\n");
    {
        struct { const char* n; int nn; } L2[] = {
            {"StrCmpW",0},{"StrCmpIW",0},{"StrCmpCW",0},{"StrCmpICW",0},{"StrCmpLogicalW",0} };
        unsigned k;
        for (k = 0; k < 5; ++k) {
            F_cmpw f = (F_cmpw)R(hsh, L2[k].n, "shlwapi");
            if (f) TIME(L2[k].n, 300000, f(A, C));
        }
        {
            F_cmpnw f = (F_cmpnw)R(hsh, "StrCmpNW", "shlwapi");
            F_cmpnw g = (F_cmpnw)R(hsh, "StrCmpNIW", "shlwapi");
            F_cmpnw h = (F_cmpnw)R(hsh, "StrCmpNCW", "shlwapi");
            F_cmpnw j = (F_cmpnw)R(hsh, "StrCmpNICW", "shlwapi");
            if (f) TIME("StrCmpNW   511", 300000, f(A, C, 511));
            if (g) TIME("StrCmpNIW  511", 300000, g(A, C, 511));
            if (h) TIME("StrCmpNCW  511", 300000, h(A, C, 511));
            if (j) TIME("StrCmpNICW 511", 300000, j(A, C, 511));
        }
        {
            /* StrRChrIW and StrRStrIW take a THREE-argument form -- (start, END, target) -- and
               calling them with two crashed this probe at exit 5 on its first run. The
               one-past-the-end pointer is what makes them searches over a RANGE rather than over a
               NUL-terminated string. */
            typedef PCWSTR (WINAPI *F_chr2)(PCWSTR, WCHAR);
            typedef PCWSTR (WINAPI *F_chr3)(PCWSTR, PCWSTR, WCHAR);
            typedef PCWSTR (WINAPI *F_str2)(PCWSTR, PCWSTR);
            typedef PCWSTR (WINAPI *F_str3)(PCWSTR, PCWSTR, PCWSTR);
            F_chr2 f  = (F_chr2)R(hsh, "StrChrIW", "shlwapi");
            F_chr3 g  = (F_chr3)R(hsh, "StrRChrIW", "shlwapi");
            F_str2 h  = (F_str2)R(hsh, "StrStrIW", "shlwapi");
            F_str3 j  = (F_str3)R(hsh, "StrRStrIW", "shlwapi");
            F_chr3 nn = (F_chr3)R(hsh, "StrChrNIW", "shlwapi");
            static wchar_t NEEDLE[] = L"xyzab";
            if (f) TIME("StrChrIW  511 (miss: worst case)", 300000, f(A, L'#'));
            if (f) TIME("StrChrIW  511 (hit at 400)",       300000, f(C, L'Z'));
            if (g) TIME("StrRChrIW 511 (range form)",       300000, g(A, A + 511, L'z'));
            if (h) TIME("StrStrIW  511",                    300000, h(A, NEEDLE));
            if (j) TIME("StrRStrIW 511 (range form)",       300000, j(A, A + 511, NEEDLE));
            if (nn) TIME("StrChrNIW 511 (range form)",      300000, nn(A, A + 511, L'#'));
        }
        {
            typedef int (WINAPI *F_spn)(PCWSTR, PCWSTR);
            F_spn f = (F_spn)R(hsh, "StrCSpnIW", "shlwapi");
            static wchar_t SET[] = L"XYZ";
            if (f) TIME("StrCSpnIW 511", 300000, f(A, SET));
        }
    }

    printf("\n== and the same comparison done ordinally by something already covered ==\n");
    {
        typedef int (WINAPI *F_cso)(PCWCH, int, PCWCH, int, BOOL);
        F_cso f = (F_cso)R(hkb, "CompareStringOrdinal", "kernelbase");
        if (f) TIME("CompareStringOrdinal 511 (covered, change 210)", 300000, f(A, 511, C, 511, FALSE));
    }
    return 0;
}
