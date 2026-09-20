/* probes/contract.c: PROVE the shipped behaviour. Nothing here is assumed from MSDN.
 *
 * The disassembly of kernelbase!FindResourceExW says:
 *    norm(x): x < 0x10000            -> x
 *             x[0] == L'#'           -> RtlUnicodeStringToInteger(x+2, 10, &v);
 *                                       fail or (v & 0xFFFF0000) -> STATUS_INVALID_PARAMETER
 *             otherwise              -> heap copy, each char through RtlUpcaseUnicodeChar
 *    hModule == 0                    -> PEB->ImageBaseAddress
 *    then LdrFindResource_U(h, {type,name,lang}, 3, &out); free; map NTSTATUS on failure.
 * Every one of those claims is checked here against the LIVE export.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG WIA_NTSTATUS;
typedef HRSRC (WINAPI *pfn_FRE)(HMODULE, LPCWSTR, LPCWSTR, WORD);
typedef WCHAR (NTAPI *pfn_Upc)(WCHAR);
typedef WIA_NTSTATUS (NTAPI *pfn_Ldr)(PVOID, const ULONG_PTR*, ULONG, void**);
static pfn_FRE FRE; static pfn_Upc RUC; static pfn_Ldr LFR;

static void show(const char* what, HMODULE m, const wchar_t* t, const wchar_t* n, WORD l)
{
    SetLastError(0xDEADBEEF);
    HRSRC r = FRE(m, t, n, l);
    printf("  %-52s -> %s  err=%lu\n", what, r ? "HRSRC" : "NULL ", GetLastError());
}

int main(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    FRE = (pfn_FRE)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FindResourceExW");
    RUC = (pfn_Upc)GetProcAddress(nt, "RtlUpcaseUnicodeChar");
    LFR = (pfn_Ldr)GetProcAddress(nt, "LdrFindResource_U");

    printf("== [Q1] RtlUpcaseUnicodeChar over the whole UTF-16 code unit space ==\n");
    int ascii_bad = 0, changed_below80 = 0, changed_total = 0, first_change = -1;
    for (int c = 0; c < 0x10000; ++c) {
        WCHAR u = RUC((WCHAR)c);
        WCHAR ascii = (c >= 'a' && c <= 'z') ? (WCHAR)(c - 32) : (WCHAR)c;
        if (u != (WCHAR)c) { ++changed_total; if (first_change < 0 && c >= 0x80) first_change = c; }
        if (c < 0x80) { if (u != ascii) { ++ascii_bad; printf("   ASCII EXCEPTION U+%04X -> U+%04X\n", c, u); }
                        if (u != (WCHAR)c) ++changed_below80; }
    }
    printf("   code units 0x00-0x7F that differ from the plain a-z fold: %d\n", ascii_bad);
    printf("   code units 0x00-0x7F that the upcase changes at all:      %d (expect 26)\n", changed_below80);
    printf("   code units in 0x0000-0xFFFF that change:                  %d\n", changed_total);
    printf("   first changing code unit at or above 0x80:                U+%04X\n", first_change);

    HMODULE user = LoadLibraryW(L"user32.dll");
    HMODULE shell = LoadLibraryExW(L"shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);

    printf("\n== [Q2] the '#' decimal form ==\n");
    show("type #6 name \"#45\"            (exists)", user, MAKEINTRESOURCEW(6), L"#45", 0);
    show("type \"#6\" name \"#45\"          (both decimal)", user, L"#6", L"#45", 0);
    show("type \"#0006\" name \"#0000045\"  (leading zeros)", user, L"#0006", L"#0000045", 0);
    show("type \"#6\" name \"#65535\"       (max 16-bit)", user, L"#6", L"#65535", 0);
    show("type \"#6\" name \"#65536\"       (overflows 16 bits)", user, L"#6", L"#65536", 0);
    show("type \"#6\" name \"#4294967296\"  (overflows 32 bits)", user, L"#6", L"#4294967296", 0);
    show("type \"#6\" name \"#\"            (nothing after the hash)", user, L"#6", L"#", 0);
    show("type \"#6\" name \"#abc\"         (not a number)", user, L"#6", L"#abc", 0);
    show("type \"#6\" name \"# 45\"         (space)", user, L"#6", L"# 45", 0);
    show("type \"#6\" name \"#+45\"         (plus)", user, L"#6", L"#+45", 0);
    show("type \"#6\" name \"#-45\"         (minus)", user, L"#6", L"#-45", 0);
    show("type \"#6\" name \"#45x\"         (trailing junk)", user, L"#6", L"#45x", 0);
    show("type \"#6\" name \"#0x2d\"        (hex-looking)", user, L"#6", L"#0x2d", 0);
    show("type \"#6\" name \"#45 \"         (trailing space)", user, L"#6", L"#45 ", 0);

    printf("\n== [Q3] NULL arguments ==\n");
    show("hModule NULL, type #6 name #45 (this .exe)", NULL, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0);
    show("type NULL (== ID 0)", user, NULL, MAKEINTRESOURCEW(45), 0);
    show("name NULL (== ID 0)", user, MAKEINTRESOURCEW(6), NULL, 0);
    show("type ID 0, name ID 0", user, MAKEINTRESOURCEW(0), MAKEINTRESOURCEW(0), 0);
    show("hModule NULL + type NULL", NULL, NULL, NULL, 0);

    printf("\n== [Q4] does SUCCESS disturb the last error? ==\n");
    SetLastError(12345);
    HRSRC r = FRE(user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0);
    printf("  after a successful call, GetLastError() = %lu (%s), r=%p\n",
           GetLastError(), GetLastError()==12345 ? "PRESERVED" : "CHANGED", (void*)r);

    printf("\n== [Q5] failure at each level ==\n");
    show("shell32 type absent", shell, MAKEINTRESOURCEW(5000), MAKEINTRESOURCEW(1), 0);
    show("shell32 name absent (type exists)", shell, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(60000), 0);
    show("shell32 lang absurd (0x0C0C)", shell, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0x0C0C);
    show("shell32 lang 0xFFFF", shell, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0xFFFF);
    show("absent string type in shell32", shell, L"NOSUCHTYPE", MAKEINTRESOURCEW(1), 0);
    show("bad hModule (0x30000)", (HMODULE)0x30000, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0);

    printf("\n== [Q6] case folding of the query, on a real module ==\n");
    HMODULE mfc = LoadLibraryExW(L"mfc140u.dll", NULL, LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    show("mfc \"PNG\"/\"AQUA_IDB_OFFICE2007_GRIPPER\"", mfc, L"PNG", L"AQUA_IDB_OFFICE2007_GRIPPER", 0);
    show("mfc \"png\"/\"aqua_idb_office2007_gripper\"", mfc, L"png", L"aqua_idb_office2007_gripper", 0);
    show("mfc \"PnG\"/\"AqUa_IdB_OfFiCe2007_GrIpPeR\"", mfc, L"PnG", L"AqUa_IdB_OfFiCe2007_GrIpPeR", 0);

    printf("\n== [Q7] a very long name, and one with non-ASCII ==\n");
    {
        static wchar_t big[200000];
        for (int i = 0; i < 199999; ++i) big[i] = L'A'; big[199999] = 0;
        show("shell32 absent name of 199999 chars", shell, MAKEINTRESOURCEW(5000), big, 0);
        wchar_t g[8]; g[0]=0x03B1; g[1]=0x00DF; g[2]=0x0131; g[3]=0xFFFD; g[4]=0xD800; g[5]=0xDC00; g[6]=0; 
        show("shell32 absent name with Greek/sharp-s/dotless/surrogates", shell, MAKEINTRESOURCEW(5000), g, 0);
        wchar_t e[2] = { 0, 0 };
        show("empty string name L\"\"", shell, MAKEINTRESOURCEW(5000), e, 0);
        show("empty string TYPE L\"\"", shell, e, MAKEINTRESOURCEW(1), 0);
    }

    printf("\n== [Q8] what the wLanguage word does with its high bits ==\n");
    show("user32 RT_STRING #45 lang 0x0409", user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0x0409);
    show("user32 RT_STRING #45 lang 0x0809", user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0x0809);
    show("user32 RT_STRING #45 lang 0x0000", user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0x0000);
    return 0;
}
