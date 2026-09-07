/* How does PathRemoveFileSpecW treat runs of backslashes, leading vs interior? */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
typedef BOOL (WINAPI *F)(PWSTR);
static F S;
static wchar_t buf[64];
static void t(const wchar_t* s){
    wcscpy(buf, s);
    BOOL r = S(buf);
    printf("  [%-12ls] -> %d  [%ls]   (len %zu -> %zu)\n", s, !!r, buf, wcslen(s), wcslen(buf));
}
int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    S = (F)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "PathRemoveFileSpecW");
    printf("leading runs of backslashes:\n");
    t(L"\\a"); t(L"\\\\a"); t(L"\\\\\\a"); t(L"\\\\\\\\a"); t(L"\\\\\\\\\\a");
    printf("\nleading runs with nothing after:\n");
    t(L"\\"); t(L"\\\\"); t(L"\\\\\\"); t(L"\\\\\\\\");
    printf("\ninterior runs:\n");
    t(L"a\\b"); t(L"a\\\\b"); t(L"a\\\\\\b"); t(L"a\\\\\\\\b");
    printf("\ninterior runs at the end:\n");
    t(L"a\\"); t(L"a\\\\"); t(L"a\\\\\\");
    printf("\ntwo interior runs:\n");
    t(L"a\\b\\c"); t(L"a\\\\b\\\\c"); t(L"ab\\\\cd\\\\ef");
    printf("\nUNC-looking:\n");
    t(L"\\\\srv\\share\\file"); t(L"\\\\srv\\share"); t(L"\\\\srv"); t(L"\\\\srv\\");
    printf("\ndrive:\n");
    t(L"a:"); t(L"a:\\"); t(L"a:\\b"); t(L"a:b"); t(L"ab:c"); t(L"a:\\\\b");
    printf("\ncolon not at index 1:\n");
    t(L":"); t(L":\\"); t(L":\\\\"); t(L"aa:"); t(L"aa:\\b");
    return 0;
}
