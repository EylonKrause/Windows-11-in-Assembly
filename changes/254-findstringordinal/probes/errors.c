/* changes/254-findstringordinal/probes/errors.c
 *
 * The refusal contract. FindStringOrdinal is a Win32 API, so unlike RtlFindUnicodeSubstring it does
 * not merely compute; it VALIDATES, sets a last-error, and returns -1. Every one of those refusals
 * is observable and has to be reproduced exactly, and the disassembly shows several that no
 * documentation would lead you to guess:
 *
 *     000A1EC3  mov r8d, dword ptr [rsp + 0x88]     <- bIgnoreCase
 *     000A1ECB  cmp r8d, 1
 *     000A1ECF  ja  0x1800a245e                     <- A "BOOL" THAT REJECTS 2
 *     000A1ED5  test rdi, rdi / je                  <- lpStringSource NULL
 *     000A1EDE  test r14, r14 / je                  <- lpStringValue NULL
 *     000A1EE7  cmp ebx, -1 / jl                    <- cchSource  < -1
 *     000A1EF8  cmp r10d, -1 / jl                   <- cchValue   < -1
 *     000A1F04  bts edx, 0x16                       <- default to FIND_FROMSTART when no
 *     000A1F08  test esi, 0xf00000 / cmovne         <-   FIND_* bit is set at all
 *     000A1F13  and eax, 0xfff00000 / dec / and     <- exactly ONE FIND_* bit may be set
 *     000A1F24  test edx, 0xff0fffff / sete         <- and NO other bit may be set
 *     000A215E  mov ecx, 0x3ec / call               <- 1004 = ERROR_INVALID_FLAGS
 *
 * `cmp r8d, 1 / ja` is the one worth the whole probe. Every Win32 convention says a BOOL is
 * "nonzero is true", and this function rejects 2 outright. A reimplementation that tested
 * `if (bIgnoreCase)` would be wrong on an input real callers can produce by passing a raw comparison
 * result or a bit-test.
 *
 * This asks the export for every one of those, including the last-error value, rather than reading
 * them off the listing.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define F_STARTSWITH 0x00100000
#define F_ENDSWITH   0x00200000
#define F_FROMSTART  0x00400000
#define F_FROMEND    0x00800000

typedef int (WINAPI *FFSO)(DWORD, LPCWSTR, int, LPCWSTR, int, BOOL);
static FFSO fso;

static void row(const char* what, DWORD fl, LPCWSTR s, int cs, LPCWSTR v, int cv, BOOL ic)
{
    int r;
    DWORD e;
    SetLastError(0xDEADBEEF);
    r = fso(fl, s, cs, v, cv, ic);
    e = GetLastError();
    printf("  %-46s -> %5d   lastError=%s\n", what, r,
           e == 0xDEADBEEF ? "(untouched)" :
           e == 0    ? "0" :
           e == 87   ? "87 ERROR_INVALID_PARAMETER" :
           e == 1004 ? "1004 ERROR_INVALID_FLAGS" : "other");
    if (e != 0xDEADBEEF && e != 0 && e != 87 && e != 1004) printf("        (lastError = %lu)\n", e);
}

int main(void)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    static const wchar_t* H = L"abcXYZabcXYZ";
    if (!k) k = LoadLibraryW(L"kernelbase.dll");
    fso = (FFSO)GetProcAddress(k, "FindStringOrdinal");
    if (!fso) { printf("resolve failed\n"); return 1; }

    printf("FindStringOrdinal -- the refusal contract\n\n");

    printf("THE FLAGS\n");
    row("flags = 0 (no FIND_* bit at all)",        0,             H, -1, L"abc", -1, FALSE);
    row("FROMSTART",                               F_FROMSTART,   H, -1, L"abc", -1, FALSE);
    row("FROMEND",                                 F_FROMEND,     H, -1, L"abc", -1, FALSE);
    row("STARTSWITH",                              F_STARTSWITH,  H, -1, L"abc", -1, FALSE);
    row("ENDSWITH",                                F_ENDSWITH,    H, -1, L"abc", -1, FALSE);
    row("FROMSTART|FROMEND (two FIND_ bits)",      F_FROMSTART|F_FROMEND, H, -1, L"abc", -1, FALSE);
    row("STARTSWITH|ENDSWITH",                     F_STARTSWITH|F_ENDSWITH, H, -1, L"abc", -1, FALSE);
    row("FROMSTART | 1 (a stray low bit)",         F_FROMSTART|1, H, -1, L"abc", -1, FALSE);
    row("FROMSTART | 0x01000000 (a stray high bit)", F_FROMSTART|0x01000000, H, -1, L"abc", -1, FALSE);
    row("0x00000001 alone",                        1,             H, -1, L"abc", -1, FALSE);
    row("0x10000000 alone",                        0x10000000,    H, -1, L"abc", -1, FALSE);
    row("LINGUISTIC_IGNORECASE 0x10 with FROMSTART", F_FROMSTART|0x10, H, -1, L"abc", -1, FALSE);

    printf("\nTHE bIgnoreCase \"BOOL\"\n");
    row("bIgnoreCase = 0",                         F_FROMSTART, H, -1, L"ABC", -1, 0);
    row("bIgnoreCase = 1",                         F_FROMSTART, H, -1, L"ABC", -1, 1);
    row("bIgnoreCase = 2  <== a BOOL that rejects 2", F_FROMSTART, H, -1, L"ABC", -1, 2);
    row("bIgnoreCase = -1 (TRUE from a bit test)",  F_FROMSTART, H, -1, L"ABC", -1, -1);

    printf("\nTHE POINTERS AND LENGTHS\n");
    row("lpStringSource NULL",                     F_FROMSTART, 0,  -1, L"abc", -1, FALSE);
    row("lpStringValue NULL",                      F_FROMSTART, H,  -1, 0,      -1, FALSE);
    row("cchSource = -1 (NUL-terminated)",         F_FROMSTART, H,  -1, L"abc", -1, FALSE);
    row("cchSource = -2",                          F_FROMSTART, H,  -2, L"abc", -1, FALSE);
    row("cchValue  = -2",                          F_FROMSTART, H,  -1, L"abc", -2, FALSE);
    row("cchSource = 0",                           F_FROMSTART, H,   0, L"abc", -1, FALSE);
    row("cchValue  = 0 (an empty needle)",         F_FROMSTART, H,  -1, L"abc",  0, FALSE);
    row("cchSource = 0, cchValue = 0",             F_FROMSTART, H,   0, L"abc",  0, FALSE);
    row("cchValue = 0, FROMEND",                   F_FROMEND,   H,  -1, L"abc",  0, FALSE);
    row("cchValue = 0, ENDSWITH",                  F_ENDSWITH,  H,  -1, L"abc",  0, FALSE);
    row("cchValue = 0, STARTSWITH",                F_STARTSWITH,H,  -1, L"abc",  0, FALSE);
    row("needle longer than haystack",             F_FROMSTART, L"ab", -1, L"abc", -1, FALSE);
    row("both NULL but both lengths 0",            F_FROMSTART, 0,   0, 0,       0, FALSE);

    printf("\nEMBEDDED NULs -- counted, or terminated?\n");
    row("src=\"ab\\0cd\" cch=5, needle=\"cd\"",    F_FROMSTART, L"ab\0cd", 5, L"cd", 2, FALSE);
    row("src=\"ab\\0cd\" cch=5, needle=\"\\0c\"",  F_FROMSTART, L"ab\0cd", 5, L"\0c", 2, FALSE);

    printf("\nFROMEND vs FROMSTART on overlapping matches\n");
    row("FROMSTART \"aa\" in \"aaaa\"",            F_FROMSTART, L"aaaa", 4, L"aa", 2, FALSE);
    row("FROMEND   \"aa\" in \"aaaa\"",            F_FROMEND,   L"aaaa", 4, L"aa", 2, FALSE);
    row("FROMEND   \"a\"  in \"aaaa\"",            F_FROMEND,   L"aaaa", 4, L"a",  1, FALSE);
    row("ENDSWITH  \"aa\" in \"aaaa\"",            F_ENDSWITH,  L"aaaa", 4, L"aa", 2, FALSE);
    return 0;
}
