/* probes/enum.c: what resource directories actually look like on this machine.
 *
 * Question 1 of the target decision: is there any NAME (string) work in the real
 * corpus at all, and how deep are the levels the search has to binary-search?
 * Prints, per module: the type table (named vs ID), and for each type the number
 * of names, how many are strings, the longest string name, and the language count.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static int g_types, g_named_types, g_names, g_named_names, g_langs, g_maxlen;

static BOOL CALLBACK cb_lang(HMODULE m, LPCWSTR t, LPCWSTR n, WORD lang, LONG_PTR p)
{ (void)m;(void)t;(void)n;(void)lang;(void)p; ++g_langs; return TRUE; }

static BOOL CALLBACK cb_name(HMODULE m, LPCWSTR t, LPWSTR n, LONG_PTR p)
{
    ++g_names;
    if (!IS_INTRESOURCE(n)) {
        int l = (int)wcslen(n);
        ++g_named_names;
        if (l > g_maxlen) g_maxlen = l;
    }
    EnumResourceLanguagesW(m, t, n, cb_lang, p);
    return TRUE;
}

static BOOL CALLBACK cb_type(HMODULE m, LPWSTR t, LONG_PTR p)
{
    ++g_types;
    if (!IS_INTRESOURCE(t)) ++g_named_types;
    EnumResourceNamesW(m, t, cb_name, p);
    return TRUE;
}

static void one(const wchar_t* dll)
{
    HMODULE m = LoadLibraryExW(dll, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!m) { wprintf(L"%-24s  (could not map: %lu)\n", dll, GetLastError()); return; }
    g_types = g_named_types = g_names = g_named_names = g_langs = g_maxlen = 0;
    EnumResourceTypesW(m, cb_type, 0);
    wprintf(L"%-24s types=%-4d (named %-3d)  names=%-6d (string %-5d, longest %-3d)  lang-leaves=%d\n",
            dll, g_types, g_named_types, g_names, g_named_names, g_maxlen, g_langs);
    FreeLibrary(m);
}

/* For one module, print the first few STRING names found, so the timing probe can
   use a real one. */
static int shown;
static BOOL CALLBACK cb_show(HMODULE m, LPCWSTR t, LPWSTR n, LONG_PTR p)
{
    (void)m;(void)p;
    if (!IS_INTRESOURCE(n) && shown < 12) {
        if (IS_INTRESOURCE(t)) wprintf(L"    type #%u  name \"%s\" (%u chars)\n",
                                       (unsigned)(ULONG_PTR)t, n, (unsigned)wcslen(n));
        else                   wprintf(L"    type \"%s\"  name \"%s\" (%u chars)\n",
                                       t, n, (unsigned)wcslen(n));
        ++shown;
    }
    return TRUE;
}
static BOOL CALLBACK cb_show_t(HMODULE m, LPWSTR t, LONG_PTR p)
{ EnumResourceNamesW(m, t, cb_show, p); return TRUE; }

int main(void)
{
    static const wchar_t* mods[] = {
        L"user32.dll", L"kernel32.dll", L"kernelbase.dll", L"ntdll.dll", L"shell32.dll",
        L"imageres.dll", L"shlwapi.dll", L"comctl32.dll", L"gdi32.dll", L"advapi32.dll",
        L"mfc140u.dll", L"explorer.exe", L"twinui.dll", L"windows.ui.xaml.dll"
    };
    wprintf(L"== resource directory shape, live modules on this machine ==\n");
    for (int i = 0; i < (int)(sizeof(mods)/sizeof(mods[0])); ++i) one(mods[i]);

    wprintf(L"\n== string-named resources found (candidates for a NAME-path subject) ==\n");
    for (int i = 0; i < (int)(sizeof(mods)/sizeof(mods[0])); ++i) {
        HMODULE m = LoadLibraryExW(mods[i], NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (!m) continue;
        shown = 0;
        wprintf(L"  %s:\n", mods[i]);
        EnumResourceTypesW(m, cb_show_t, 0);
        if (!shown) wprintf(L"    (none -- every name is an integer ID)\n");
        FreeLibrary(m);
    }
    return 0;
}
