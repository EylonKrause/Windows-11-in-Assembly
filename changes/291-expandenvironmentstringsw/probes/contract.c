/* changes/291-expandenvironmentstringsw/probes/contract.c
 *
 * THE CONTRACT PROBE. This ran BEFORE reference.c was written, and reference.c is a transcription
 * of what it printed plus the disassembly it was checking. It exists because every one of the six
 * questions the task named is documented badly or not at all:
 *
 *     * is the return value characters or bytes, and does it include the terminating null?
 *     * when the buffer is too small: what comes back, and what is left in the buffer?
 *     * is an unmatched '%' copied through literally?
 *     * is '%%' an escape, or two literal percent signs?
 *     * is a '%VAR%' whose VAR does not exist removed, or copied through?
 *     * nSize == 0 / lpDst == NULL -- the measuring call.
 *
 * Every destination is filled with the sentinel 0xBEEF first and the number of cells that stopped
 * being the sentinel is reported, so "what was written" is a measurement and not an inference from
 * the return value. That distinction turned out to matter: on the truncating path this function
 * writes nSize-1 characters and NO terminator, which no documentation mentions.
 *
 * The last block reads ntdll's own table of virtual environment variable names, at RVA 0x173AC0 in
 * 10.0.26100.9278, because RtlQueryEnvironmentVariable consults it BEFORE it walks the process
 * environment block -- four names that are not in the block at all and still expand.
 *
 * Build:  cl /nologo /O2 probes\contract.c /Fe:contract.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef DWORD (WINAPI *pfn)(LPCWSTR, LPWSTR, DWORD);
static pfn EES;

#define SENT 0xBEEF

static void dump(const wchar_t* tag, const wchar_t* src, DWORD nSize)
{
    static wchar_t buf[512];
    DWORD i, r, le, touched = 0;
    for (i = 0; i < 512; ++i) buf[i] = SENT;
    SetLastError(0);
    r = EES(src, nSize ? buf : NULL, nSize);
    le = GetLastError();
    wprintf(L"%-28s nSize=%-4u ret=%-6u lasterr=%-5u out=[", tag, nSize, r, le);
    for (i = 0; i < 40 && i < 512; ++i) {
        if (buf[i] == SENT) { wprintf(L"<S>"); break; }
        if (buf[i] == 0) { wprintf(L"\\0"); continue; }
        wprintf(L"%c", buf[i]);
    }
    for (i = 0; i < 512; ++i) if (buf[i] != SENT) touched = i + 1;
    wprintf(L"]  cells written=%u\n", touched);
}

static void dumpsrc(const wchar_t* src, DWORD nSize) { dump(src, src, nSize); }

int main(void)
{
    HMODULE k = LoadLibraryW(L"kernel32.dll");
    static wchar_t buf[4096];
    DWORD r, n;
    EES = (pfn)GetProcAddress(k, "ExpandEnvironmentStringsW");

    wprintf(L"--- return value: characters, and the null is counted --------------------------\n");
    dumpsrc(L"", 100);            /* 1 */
    dumpsrc(L"a", 100);           /* 2 */
    dumpsrc(L"abc", 100);         /* 4 */
    dump(L"NULL-src", NULL, 100); /* 1, and a null IS written */
    dump(L"NULL-src-n0", NULL, 0);

    wprintf(L"\n--- the percent forms -----------------------------------------------------------\n");
    dumpsrc(L"%", 100);                       /* unmatched -> literal            */
    dumpsrc(L"%%", 100);                      /* NOT an escape -> "%%"           */
    dumpsrc(L"%%%", 100);
    dumpsrc(L"a%b", 100);
    dumpsrc(L"%TEMP", 100);
    dumpsrc(L"TEMP%", 100);
    dumpsrc(L"%TEMP%", 200);
    dumpsrc(L"%temp%", 200);                  /* the name is case-insensitive    */
    dumpsrc(L"%%TEMP%%", 200);                /* "%" + value + "%"               */
    dumpsrc(L"%nOnSeNsE_NoT_SeT_1234%", 200); /* unset -> the whole thing stays  */
    dumpsrc(L"x%nOnSeNsE_NoT_SeT_1234%y", 200);
    dumpsrc(L"%=C:%", 200);                   /* names may start with '='        */

    wprintf(L"\n--- the measuring call ----------------------------------------------------------\n");
    dumpsrc(L"abc", 0);
    dumpsrc(L"%TEMP%", 0);

    wprintf(L"\n--- every destination size around the exact fit ---------------------------------\n");
    for (n = 0; n <= 9; ++n) dump(L"abcdef(needs 7)", L"abcdef", n);

    wprintf(L"\n--- a variable that does not fit: ONE null is written, nothing else --------------\n");
    {
        DWORD need = EES(L"%TEMP%", NULL, 0);
        wprintf(L"need(%%TEMP%%)=%u\n", need);
        dump(L"exact",   L"%TEMP%", need);
        dump(L"short-1", L"%TEMP%", need - 1);
        dump(L"half",    L"%TEMP%", need / 2);
        dump(L"n=1",     L"%TEMP%", 1);
    }

    wprintf(L"\n--- last error is NOT set when the buffer is too small ---------------------------\n");
    SetLastError(12345);
    r = EES(L"abcdef", buf, 3);
    wprintf(L"ret=%u lasterr=%u (12345 means untouched)\n", r, GetLastError());

    wprintf(L"\n--- ntdll's virtual variables, read out of its own table ------------------------\n");
    {
        unsigned char* base = (unsigned char*)GetModuleHandleW(L"ntdll.dll");
        unsigned char* tab  = base + 0x173AC0;          /* 10.0.26100.9278 */
        int i;
        for (i = 0; i < 4; ++i) {
            unsigned __int64 len = *(unsigned __int64*)(tab + i * 24);
            wchar_t*         nm  = *(wchar_t**)(tab + i * 24 + 8);
            unsigned         id  = *(unsigned*)(tab + i * 24 + 16);
            if (len > 64) { wprintf(L"  (table moved in this build)\n"); break; }
            wprintf(L"  entry[%d] len=%llu id=%u name=\"%.*s\"\n", i,
                    (unsigned long long)len, id, (int)len, nm);
        }
    }

    wprintf(L"\n--- no 0x7FFF cap: kernelbase uses the SIZE_T form of the Rtl routine -----------\n");
    {
        static wchar_t big[70000];
        DWORD i;
        for (i = 0; i < 69999; ++i) big[i] = L'a';
        big[69999] = 0;
        wprintf(L"big(69999): ret=%u\n", EES(big, NULL, 0));
    }
    return 0;
}
