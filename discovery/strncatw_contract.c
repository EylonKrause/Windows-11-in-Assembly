/* discovery/strncatw_contract.c
   shlwapi!StrNCatW at the edges, before it is reimplemented.

   WHY. discovery/shlwapi_str_c.c timed StrNCatW at 0.34 ns per character (2796 ns for 4096 onto 4096),
   a one-character loop like its siblings. Its documentation says cchMax is "the number of characters to
   be appended", which is ambiguous about the terminator; Wine implements it as
   StrCpyNW(dst + strlenW(dst), src, cchMax), which would make it change 170's StrCatBuffW with a
   different bound. Nothing here is assumed from either:

     1. return value, NULL dst, NULL src;
     2. how many characters are appended for every cchMax from -3 to past the source, and whether a
        terminator is written when the copy is cut short, or when cchMax <= 0;
     3. an UNTERMINATED destination running into NOACCESS: fault, and is anything written?
     4. an UNTERMINATED source running into NOACCESS with a large cchMax: fault, and how many
        characters were written before it -- every readable one, as StrCpyW (change 303)?
     5. a destination that overlaps the source.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef PWSTR (WINAPI *PNCAT)(PWSTR, PCWSTR, int);
static PNCAT NCat;

static void show(const wchar_t* s, int n) {
    printf("[");
    for (int i = 0; i < n; ++i) { wchar_t c = s[i]; if (c >= 32 && c < 127) putchar((char)c); else if (c == 0) printf("0"); else printf("<%X>", c); }
    printf("]");
}

int main(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    NCat = (PNCAT)GetProcAddress(h, "StrNCatW");
    if (!NCat) { printf("no StrNCatW\n"); return 2; }
    {
        HMODULE m = 0; wchar_t path[MAX_PATH] = L"?";
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)NCat, &m);
        GetModuleFileNameW(m, path, MAX_PATH);
        printf("StrNCatW at %p in %ls\n", (void*)NCat, path);
    }

    printf("\n== 1. return value and NULLs ==\n");
    {
        wchar_t d[16] = L"abc";
        PWSTR r = NCat(d, L"de", 5);
        printf("  NCat(\"abc\",\"de\",5) returned %s, dst ", r == d ? "dst" : "OTHER"); show(d, 8); printf("\n");
        __try { r = NCat(NULL, L"de", 5); printf("  NCat(NULL,\"de\",5) -> %p\n", (void*)r); } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  NCat(NULL,\"de\",5) -> FAULT\n"); }
        wcscpy(d, L"abc");
        __try { r = NCat(d, NULL, 5); printf("  NCat(dst,NULL,5) -> %s, dst ", r == d ? "dst" : r ? "OTHER" : "NULL"); show(d, 8); printf("\n"); } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  NCat(dst,NULL,5) -> FAULT\n"); }
        __try { r = NCat(NULL, NULL, 0); printf("  NCat(NULL,NULL,0) -> %p\n", (void*)r); } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  NCat(NULL,NULL,0) -> FAULT\n"); }
        wcscpy(d, L"abc");
        __try { r = NCat(d, NULL, 0); printf("  NCat(dst,NULL,0) -> %s\n", r == d ? "dst" : r ? "OTHER" : "NULL"); } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  NCat(dst,NULL,0) -> FAULT\n"); }
    }

    printf("\n== 2. cchMax from -3 to 8, dst \"abc\" + src \"defgh\" (buffer pre-filled with '#') ==\n");
    for (int n = -3; n <= 8; ++n) {
        wchar_t d[16];
        for (int i = 0; i < 16; ++i) d[i] = L'#';
        d[0] = L'a'; d[1] = L'b'; d[2] = L'c'; d[3] = 0;
        NCat(d, L"defgh", n);
        printf("  n=%2d: ", n); show(d, 11); printf("\n");
    }
    {
        wchar_t d[16];
        for (int i = 0; i < 16; ++i) d[i] = L'#';
        d[0] = 0;
        NCat(d, L"xy", 2); printf("  empty dst, \"xy\", n=2: "); show(d, 5); printf("\n");
        for (int i = 0; i < 16; ++i) d[i] = L'#';
        d[0] = L'a'; d[1] = 0;
        NCat(d, L"", 4); printf("  \"a\" + empty src, n=4: "); show(d, 5); printf("\n");
    }

    printf("\n== 3. unterminated DESTINATION into NOACCESS ==\n");
    {
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        wchar_t* d = (wchar_t*)(b + 0x2000) - 8;
        for (int i = 0; i < 8; ++i) d[i] = L'x';
        int f = 0;
        __try { NCat(d, L"abc", 10); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("  %s; dst now ", f ? "FAULT" : "no fault"); show(d, 8); printf("\n");
    }

    printf("\n== 4. unterminated SOURCE into NOACCESS, cchMax 1000 ==\n");
    {
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        static const int L[] = { 1, 3, 17, 40 };
        for (int k = 0; k < 4; ++k) {
            wchar_t* s = (wchar_t*)(b + 0x2000) - L[k];
            for (int i = 0; i < L[k]; ++i) s[i] = (wchar_t)(L'a' + i % 26);
            static wchar_t d[100];
            for (int i = 0; i < 100; ++i) d[i] = L'#';
            d[0] = L'Z'; d[1] = 0;
            int f = 0;
            __try { NCat(d, s, 1000); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
            int w = 0; while (w < 99 && d[1 + w] != L'#') ++w;
            printf("  %2d readable chars: %s, %d chars written after the old terminator position\n", L[k], f ? "FAULT" : "no fault", w);
        }
        /* and with cchMax cutting the copy exactly at the page end: no fault expected if index n-1 is not read */
        wchar_t* s = (wchar_t*)(b + 0x2000) - 5;
        for (int i = 0; i < 5; ++i) s[i] = L'q';
        static wchar_t d[16];
        for (int n = 4; n <= 7; ++n) {
            for (int i = 0; i < 16; ++i) d[i] = L'#';
            d[0] = 0;
            int f = 0;
            __try { NCat(d, s, n); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
            printf("  5 readable chars, n=%d: %s, dst ", n, f ? "FAULT" : "no fault"); show(d, 8); printf("\n");
        }
    }

    printf("\n== 5. overlap: src inside dst's tail ==\n");
    {
        wchar_t d[32];
        wcscpy(d, L"abcdef");
        NCat(d, d + 2, 10);          /* appends "cdef..." onto itself */
        printf("  NCat(d=\"abcdef\", d+2, 10): "); show(d, 14); printf("\n");
    }
    return 0;
}
