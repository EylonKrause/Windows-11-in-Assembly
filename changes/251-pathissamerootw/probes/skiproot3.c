/* Ground truth for PathCchSkipRoot's "\\?" family, read off rather than guessed.
 *
 * The disassembly settles everything else:
 *   - s[0] not a separator  -> drive branch: alpha + ':' -> 2, or 3 if followed by '\'
 *   - s[0] a separator, s[1] not -> 1
 *   - s[0..1] separators, s[2] != '?' -> the UNC walk from index 2, which at 0x2B47C is exactly
 *       wcschr for the server separator, then wcschr for the share separator, with
 *       `cmove rbx, rax` making the share's separator count only when the share is NON-EMPTY
 *   - s[0..1] separators, s[2] == '?' -> 0x2B4CD, a case-insensitive 5-character compare of
 *       s[3..7] against "\UNC\"; on a match, the UNC walk from index 8.
 *
 * What is not settled is what happens when that "\unc\" compare fails. Empirically "\\?\c:" is 6,
 * "\\?\a" is an error, and "\\?\Volume{...}\" is 49 -- so it is neither "always the drive rule" nor
 * "fall back to the plain UNC walk". This dumps the family so the rule can be read.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *FSKIP)(const wchar_t*, const wchar_t**);
static FSKIP skip;

static int val(const wchar_t* s)
{
    const wchar_t* e = 0;
    HRESULT hr = skip(s, &e);
    return (hr < 0) ? -1 : (int)(e - s);
}

static void row(const wchar_t* s)
{
    int i;
    printf("   %-34ls -> %3d   |", s, val(s));
    for (i = 0; s[i]; ++i) printf("%c", (s[i] >= 32 && s[i] < 127) ? (char)s[i] : '?');
    printf("|\n");
}

int main(void)
{
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    skip = (FSKIP)GetProcAddress(hk, "PathCchSkipRoot");
    if (!skip) { printf("resolve failed\n"); return 1; }

    printf("PathCchSkipRoot: the \"\\\\?\" family, ground truth\n\n");

    printf("1. what follows \"\\\\?\" -- the branch is taken on s[2]=='?' ALONE\n");
    row(L"\\\\?");           row(L"\\\\?a");        row(L"\\\\?\\");
    row(L"\\\\?\\a");        row(L"\\\\?\\a\\");    row(L"\\\\?\\a\\b");
    row(L"\\\\?\\ab");       row(L"\\\\?\\ab\\");   row(L"\\\\?\\ab\\c");
    row(L"\\\\?\\abc\\d\\e");
    printf("\n");

    printf("2. the drive form behind the prefix\n");
    row(L"\\\\?\\C:");       row(L"\\\\?\\C:\\");   row(L"\\\\?\\C:\\a");
    row(L"\\\\?\\C:a");      row(L"\\\\?\\1:");     row(L"\\\\?\\::");
    row(L"\\\\?\\C");        row(L"\\\\?\\C:\\\\");
    printf("\n");

    printf("3. the UNC form behind the prefix (the 5-char \"\\UNC\\\" compare at 0x2B4CD)\n");
    row(L"\\\\?\\UNC");      row(L"\\\\?\\UNC\\");  row(L"\\\\?\\UNC\\s");
    row(L"\\\\?\\UNC\\s\\"); row(L"\\\\?\\UNC\\s\\h"); row(L"\\\\?\\UNC\\s\\h\\");
    row(L"\\\\?\\UNC\\s\\h\\a"); row(L"\\\\?\\unc\\s\\h\\"); row(L"\\\\?\\UnC\\s\\h");
    row(L"\\\\?\\UNCX\\s");  row(L"\\\\?\\UN\\s");  row(L"\\\\?\\UNC\\\\h");
    printf("\n");

    printf("4. volume-style names: how long must the name be, and does it need a separator?\n");
    row(L"\\\\?\\V");        row(L"\\\\?\\V\\");    row(L"\\\\?\\Vo");
    row(L"\\\\?\\Vo\\");     row(L"\\\\?\\Volume"); row(L"\\\\?\\Volume\\");
    row(L"\\\\?\\Volume\\x");row(L"\\\\?\\Volume{1}\\");
    row(L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}");
    row(L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}\\");
    row(L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}\\x");
    printf("\n");

    printf("5. the same shapes behind \"\\\\.\" instead of \"\\\\?\" -- is '.' special at all?\n");
    row(L"\\\\.");           row(L"\\\\.a");        row(L"\\\\.\\");
    row(L"\\\\.\\a");        row(L"\\\\.\\a\\");    row(L"\\\\.\\C:");
    row(L"\\\\.\\C:\\");     row(L"\\\\.\\UNC\\s\\h\\"); row(L"\\\\.\\PhysicalDrive0");
    printf("\n");

    printf("6. and a plain UNC, for comparison\n");
    row(L"\\\\s");           row(L"\\\\s\\");       row(L"\\\\s\\h");
    row(L"\\\\s\\h\\");      row(L"\\\\s\\h\\a");   row(L"\\\\s\\\\h");
    row(L"\\\\\\h");         row(L"\\\\\\");
    return 0;
}
