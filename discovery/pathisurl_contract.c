/* discovery/pathisurl_contract.c
   shlwapi/kernelbase PathIsURLW and PathIsURLA at the edges.

   WHY. discovery/shlwapi_path3.c timed PathIsURLW at 0.82 ns per character on a bare file name and
   2.67 ns on a rooted path: data-dependent, a scan that only stops early when it can. The disassembly
   (kernelbase RVA 0x45AF0) is one loop over the start of the string -- stop on NUL, ':' or a unit that
   fails a class-table test -- and TRUE exactly when it stopped on ':' after two or more units; what
   follows the ':' (a scheme-table lookup and a wcslen under __try) cannot change the answer. So:

     1. the set of units the scan accepts, every unit 0..0xFFFF, at index 0 and at index 2;
     2. the rule at ':' -- index 0, 1, 2, 3;
     3. that nothing after ':' matters: an unreadable page right after the ':' does not fault, and
        garbage after it does not change the answer;
     4. NULL; an unterminated scheme run into NOACCESS (faults, presumably);
     5. PathIsURLA: the same questions on bytes.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    /* 1 */
    {
        int acc[128] = { 0 }, nonascii = 0, first = -1;
        for (int c = 1; c < 0x10000; ++c) {
            if (c == ':') continue;
            wchar_t s[8] = { 'a', 'b', (wchar_t)c, 'x', ':', 0 };
            BOOL r = PathIsURLW(s);
            wchar_t t[8] = { (wchar_t)c, 'a', 'b', ':', 0 };
            BOOL r0 = PathIsURLW(t);
            if (r != r0) printf("  unit %04X: index 2 says %d, index 0 says %d\n", c, r, r0);
            if (c < 128) acc[c] = r; else if (r) { ++nonascii; if (first < 0) first = c; }
        }
        printf("== 1. accepted units: ");
        for (int c = 1; c < 128; ++c) if (acc[c]) putchar(c);
        printf("\n   non-ASCII units accepted: %d (first %04X)\n", nonascii, first);
    }
    /* 2 */
    printf("\n== 2. ':' at index 0..3: %d %d %d %d   (\"C:\\\\x\" %d, \"ab:\" %d, \"http://x\" %d, \"file:///c:/x\" %d)\n",
           PathIsURLW(L":x"), PathIsURLW(L"a:x"), PathIsURLW(L"ab:x"), PathIsURLW(L"abc:x"),
           PathIsURLW(L"C:\\x"), PathIsURLW(L"ab:"), PathIsURLW(L"http://x"), PathIsURLW(L"file:///c:/x"));
    printf("   unknown scheme \"zzzz:x\" %d, mixed case \"HtTp:x\" %d, digit-led \"1ab:x\" %d, \"a+b-c.d:x\" %d\n",
           PathIsURLW(L"zzzz:x"), PathIsURLW(L"HtTp:x"), PathIsURLW(L"1ab:x"), PathIsURLW(L"a+b-c.d:x"));
    /* 3 + 4 */
    {
        unsigned char* pg = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(pg, 0x1000, MEM_COMMIT, PAGE_READWRITE);
        wchar_t* s = (wchar_t*)(pg + 0x1000) - 5;
        s[0] = 'h'; s[1] = 't'; s[2] = 't'; s[3] = 'p'; s[4] = ':';
        int f = 0; BOOL r = 0;
        __try { r = PathIsURLW(s); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("\n== 3. \"http:\" with NOACCESS right after the ':': %s %d\n", f ? "FAULT" : "no fault, returns", r);
        wchar_t* t = (wchar_t*)(pg + 0x1000) - 6;
        for (int i = 0; i < 6; ++i) t[i] = (wchar_t)('a' + i);
        f = 0;
        __try { r = PathIsURLW(t); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("== 4. six scheme units, unterminated, into NOACCESS: %s %d;  NULL -> %d\n", f ? "FAULT" : "no fault", r, PathIsURLW(NULL));
        char* a = (char*)pg + 0x1000 - 6; memcpy(a, "abcdef", 6);
        f = 0;
        __try { r = PathIsURLA(a); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("   PathIsURLA, the same: %s;  NULL -> %d\n", f ? "FAULT" : "no fault", PathIsURLA(NULL));
        char* b = (char*)pg + 0x1000 - 5; memcpy(b, "http:", 5);
        f = 0;
        __try { r = PathIsURLA(b); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("   PathIsURLA \"http:\" with NOACCESS after ':': %s %d\n", f ? "FAULT" : "no fault, returns", r);
    }
    /* 5 */
    {
        int acc[256] = { 0 }, dw = 0;
        for (int c = 1; c < 256; ++c) {
            if (c == ':') continue;
            char s[8] = { 'a', 'b', (char)c, 'x', ':', 0 };
            acc[c] = PathIsURLA(s);
            wchar_t w[8] = { 'a', 'b', (wchar_t)c, 'x', ':', 0 };
            if (c < 128 && acc[c] != PathIsURLW(w)) ++dw;
        }
        printf("\n== 5. PathIsURLA accepted bytes: ");
        for (int c = 1; c < 128; ++c) if (acc[c]) putchar(c);
        int hi = 0; for (int c = 128; c < 256; ++c) hi += acc[c];
        printf("\n   high bytes accepted: %d; differs from the W set below 0x80 on %d\n", hi, dw);
    }
    return 0;
}
