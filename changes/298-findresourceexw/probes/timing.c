/* probes/timing.c: is FindResourceExW a target at all?
 *
 * The tier-2 sweep could not resolve a usable subject (it asked for
 * GetModuleHandleW(L"user32.dll") in a console process that never loaded user32).
 * This probe answers the question the task actually poses, with numbers:
 *
 *   [A] ID type + ID name, present, in a module with 1 name vs one with 3570.
 *   [B] ID lookup that FAILS at each of the three levels.
 *   [C] STRING type + STRING name, present, real names of 27..38 chars.
 *   [D] STRING name ABSENT, length swept 1..1024; this is the only place where
 *       the shipped code can be length-driven, because normalisation of both
 *       arguments happens BEFORE LdrFindResource_U is called, so an absent name
 *       still pays the whole string cost.
 *   [E] the "#123" decimal form.
 *   [F] cold (first call on a freshly mapped module) vs warm.
 *
 * Disassembly says the string path is: wcslen -> RtlAllocateHeap -> a per-character
 * CALL to RtlUpcaseUnicodeChar -> LdrFindResource_U -> RtlFreeHeap.  If that is
 * real, [D] rises with length and the slope is the target.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "bench.h"

typedef HRSRC (WINAPI *pfn_FRE)(HMODULE, LPCWSTR, LPCWSTR, WORD);
static pfn_FRE FRE;

typedef struct { HMODULE m; const wchar_t* type; const wchar_t* name; WORD lang; } ctx_t;

static uint64_t op(void* c)
{
    ctx_t* x = (ctx_t*)c;
    return (uint64_t)(ULONG_PTR)FRE(x->m, x->type, x->name, x->lang);
}

static volatile uint64_t sink;

static double t(const char* label, HMODULE m, const wchar_t* type, const wchar_t* name, WORD lang)
{
    ctx_t c = { m, type, name, lang };
    HRSRC r = FRE(m, type, name, lang);
    DWORD e = r ? 0 : GetLastError();
    double ns = wia_measure(op, &c, 9, &sink);
    printf("%-58s %10.2f ns   %s%lu\n", label, ns, r ? "FOUND" : "miss err=", r ? 0UL : e);
    return ns;
}

static HMODULE map(const wchar_t* dll)
{
    HMODULE m = LoadLibraryExW(dll, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!m) { wprintf(L"  !! cannot map %s (%lu)\n", dll, GetLastError()); }
    return m;
}

int main(void)
{
    FRE = (pfn_FRE)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FindResourceExW");
    if (!FRE) { printf("no FindResourceExW\n"); return 1; }
    wia_pin(2);

    HMODULE shell = map(L"shell32.dll");     /* 19 types, 3570 names */
    HMODULE gdi   = map(L"gdi32.dll");       /*  1 type,     1 name  */
    HMODULE mfc   = map(L"mfc140u.dll");     /* 14 types, 801 names, 558 of them STRINGS */
    HMODULE user  = map(L"user32.dll");      /* 13 types,  223 names */

    printf("\n== [A] ID type + ID name, PRESENT ==\n");
    if (gdi)   t("gdi32   RT_VERSION #1        (1 type, 1 name)",   gdi,   MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), 0);
    if (user)  t("user32  RT_STRING  #45       (13 types, 223 names)", user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0);
    if (shell) t("shell32 RT_ICON    #1        (19 types, 3570 names)", shell, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0);
    if (shell) t("shell32 RT_ICON    #16000    (deep in the table)", shell, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(16000), 0);
    if (shell) t("shell32 RT_GROUP_ICON #4     ", shell, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(4), 0);

    printf("\n== [B] ID lookup that FAILS, per level ==\n");
    if (shell) t("shell32 type absent   (#5000)", shell, MAKEINTRESOURCEW(5000), MAKEINTRESOURCEW(1), 0);
    if (shell) t("shell32 name absent   (RT_ICON #60000)", shell, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(60000), 0);
    if (shell) t("shell32 lang absent   (RT_ICON #1, lang 0x0C0C)", shell, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0x0C0C);

    printf("\n== [C] STRING type + STRING name, PRESENT (mfc140u) ==\n");
    if (mfc) {
        t("mfc  \"PNG\" / \"AQUA_IDB_OFFICE2007_GRIPPER\" (27ch)", mfc, L"PNG", L"AQUA_IDB_OFFICE2007_GRIPPER", 0);
        t("mfc  \"PNG\" / \"AQUA_IDB_OFFICE2007_MENU_BTN\" (28ch)", mfc, L"PNG", L"AQUA_IDB_OFFICE2007_MENU_BTN", 0);
        t("mfc  \"PNG\" / \"AQUA_IDB_OFFICE2007_MENU_ITEM_MARKER_C\" (38ch)", mfc, L"PNG", L"AQUA_IDB_OFFICE2007_MENU_ITEM_MARKER_C", 0);
        t("mfc  \"PNG\" / lowercase spelling of the 38ch name", mfc, L"png", L"aqua_idb_office2007_menu_item_marker_c", 0);
        t("mfc  ID type #2 / ID name (control: no string at all)", mfc, MAKEINTRESOURCEW(2), MAKEINTRESOURCEW(1), 0);
    }

    printf("\n== [D] STRING name ABSENT, length swept -- the length-driven test ==\n");
    if (mfc) {
        static wchar_t buf[1200];
        static const int L[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };
        double base = 0;
        for (int i = 0; i < (int)(sizeof(L)/sizeof(L[0])); ++i) {
            int n = L[i];
            for (int k = 0; k < n; ++k) buf[k] = L'Z';
            buf[n] = 0;
            char lab[80]; sprintf(lab, "mfc  \"PNG\" / absent name of %4d chars", n);
            double ns = t(lab, mfc, L"PNG", buf, 0);
            if (i == 0) base = ns;
            if (i == (int)(sizeof(L)/sizeof(L[0])) - 1)
                printf("    ---> slope over 1..1024 chars: %.3f ns per character\n", (ns - base) / 1023.0);
        }
        /* and with the TYPE string long too */
        static wchar_t tbuf[1200];
        for (int k = 0; k < 1024; ++k) tbuf[k] = L'Q';
        tbuf[1024] = 0;
        t("mfc  absent TYPE of 1024 chars + absent name of 1024", mfc, tbuf, buf, 0);
    }

    printf("\n== [E] the \"#123\" decimal form vs MAKEINTRESOURCE ==\n");
    if (user) {
        t("user32 type \"#6\" / name \"#45\"      (decimal strings)", user, L"#6", L"#45", 0);
        t("user32 type #6   / name #45         (integers)", user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0);
        t("user32 type \"#6\" / name \"#0000000045\" (padded)", user, L"#6", L"#0000000045", 0);
    }

    printf("\n== [F] cold: FIRST call on a freshly mapped module ==\n");
    for (int i = 0; i < 3; ++i) {
        HMODULE m = LoadLibraryExW(L"comctl32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        LARGE_INTEGER a, b, f; QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        HRSRC r = FRE(m, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0);
        QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart;
        printf("  cold call #%d: %10.2f ns  (%s)\n", i, ns, r ? "FOUND" : "miss");
        QueryPerformanceCounter(&a);
        r = FRE(m, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0);
        QueryPerformanceCounter(&b);
        printf("  second call : %10.2f ns\n", (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart);
        FreeLibrary(m);
    }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
