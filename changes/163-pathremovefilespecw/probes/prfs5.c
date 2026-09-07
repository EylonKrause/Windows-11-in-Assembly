/* Does PathSkipRootW explain PathRemoveFileSpecW's run behaviour? */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
typedef BOOL  (WINAPI *RF)(PWSTR);
typedef PWSTR (WINAPI *SR)(PCWSTR);
static RF rem; static SR skip;
static wchar_t buf[64];
static void t(const wchar_t* s){
    wcscpy(buf, s);
    const wchar_t* r = skip(buf);
    int rootlen = r ? (int)(r - buf) : -1;
    wcscpy(buf, s);
    rem(buf);
    printf("  [%-10ls] len=%-2zu rootLen=%-3d -> L=%-2zu  [%ls]\n",
           s, wcslen(s), rootlen, wcslen(buf), buf);
}
int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE sh = LoadLibraryW(L"shlwapi.dll");
    rem  = (RF)GetProcAddress(sh, "PathRemoveFileSpecW");
    skip = (SR)GetProcAddress(sh, "PathSkipRootW");
    printf("leading runs (rootLen = -1 means PathSkipRootW returned NULL):\n");
    t(L"\\a"); t(L"\\\\a"); t(L"\\\\\\a"); t(L"\\\\\\\\a"); t(L"\\\\\\\\\\a");
    printf("\ninterior runs:\n");
    t(L"a\\b"); t(L"a\\\\b"); t(L"a\\\\\\b"); t(L"a\\\\\\\\b");
    printf("\nend runs:\n");
    t(L"a\\"); t(L"a\\\\"); t(L"a\\\\\\");
    printf("\nleading only:\n");
    t(L"\\"); t(L"\\\\"); t(L"\\\\\\"); t(L"\\\\\\\\");
    printf("\ndrive and UNC:\n");
    t(L"a:"); t(L"a:\\"); t(L"a:\\b"); t(L"\\\\srv\\share\\f"); t(L"\\\\srv\\share"); t(L"\\\\srv");
    printf("\nmisc:\n");
    t(L":"); t(L":\\"); t(L"aa:"); t(L"a/b"); t(L"abc");
    return 0;
}
