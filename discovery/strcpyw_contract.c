/* discovery/strcpyw_contract.c
   shlwapi!StrCpyW and StrCatW at the edges, before either is reimplemented.

   discovery/shlwapi_str_c.c timed StrCpyW at 0.446 ns per character at 4096, against 0.046 for the
   C runtime's wcscpy: a one-character-at-a-time loop. A vector copy is easy; a vector copy that is
   the SAME FUNCTION is not, because a scalar copy has observable behaviour on bad input that a block
   copy changes:

     * an unterminated source running into a NOACCESS page: a scalar loop writes every character it
       could read before it faults. Does StrCpyW? If so, how much of the destination is written?
     * NULL arguments: a fault, or a guard that returns something?
     * a destination that OVERLAPS the source just below it: a forward scalar copy behaves like
       memmove there, and a block copy has to as well.
     * StrCatW: the same questions, plus an unterminated DESTINATION.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef PWSTR (WINAPI *PCPY)(PWSTR, PCWSTR);
static PCPY Cpy, Cat;

static void show(const wchar_t* tag, const wchar_t* s, int n) {
    printf("  %-34ls [", tag);
    for (int i = 0; i < n; ++i) { wchar_t c = s[i]; if (c >= 32 && c < 127) putchar((char)c); else printf("<%X>", c); }
    printf("]\n");
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE s = LoadLibraryW(L"shlwapi.dll");
    Cpy = (PCPY)GetProcAddress(s, "StrCpyW");
    Cat = (PCPY)GetProcAddress(s, "StrCatW");

    printf("== return values ==\n");
    { wchar_t d[16]; PWSTR r = Cpy(d, L"abc"); printf("  StrCpyW returns %s\n", r == d ? "dst" : "something else");
      r = Cat(d, L"de"); printf("  StrCatW returns %s, dst \"%ls\"\n", r == d ? "dst" : "something else", d); }

    printf("\n== NULL arguments ==\n");
    {
        wchar_t d[16] = L"zz";
        PWSTR r = (PWSTR)1; int f = 0;
        __try { r = Cpy(NULL, L"abc"); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("  StrCpyW(NULL, \"abc\")  -> %s %p\n", f ? "FAULT" : "returned", (void*)r);
        r = (PWSTR)1; f = 0;
        __try { r = Cpy(d, NULL); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("  StrCpyW(dst, NULL)    -> %s %p, dst now \"%ls\"\n", f ? "FAULT" : "returned", (void*)r, d);
        r = (PWSTR)1; f = 0;
        __try { r = Cat(NULL, L"abc"); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("  StrCatW(NULL, \"abc\")  -> %s %p\n", f ? "FAULT" : "returned", (void*)r);
        r = (PWSTR)1; f = 0;
        __try { r = Cat(d, NULL); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("  StrCatW(dst, NULL)    -> %s %p, dst now \"%ls\"\n", f ? "FAULT" : "returned", (void*)r, d);
    }

    printf("\n== unterminated source running into NOACCESS ==\n");
    {
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        int ns[] = { 3, 17, 40 };
        for (int k = 0; k < 3; ++k) {
            int n = ns[k];
            wchar_t* src = (wchar_t*)(b + 0x2000) - n;
            for (int i = 0; i < n; ++i) src[i] = (wchar_t)(L'a' + i % 26);
            wchar_t dst[64]; for (int i = 0; i < 64; ++i) dst[i] = L'#';
            int f = 0;
            __try { Cpy(dst, src); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
            int written = 0; for (int i = 0; i < 64; ++i) if (dst[i] != L'#') written = i + 1;
            printf("  StrCpyW, %2d readable chars, no NUL: %s, %d chars written to dst\n", n, f ? "FAULT" : "returned", written);
        }
    }

    printf("\n== overlap: destination just BELOW the source (dst = src - g) ==\n");
    for (int g = 1; g <= 20; g += (g < 4 ? 1 : 7)) {
        wchar_t buf[96]; wchar_t ref[96];
        for (int i = 0; i < 96; ++i) buf[i] = ref[i] = L'.';
        for (int i = 0; i < 40; ++i) buf[30 + i] = ref[30 + i] = (wchar_t)(L'A' + i % 26);
        buf[70] = ref[70] = 0;
        Cpy(buf + 30 - g, buf + 30);
        memmove(ref + 30 - g, ref + 30, 41 * 2);
        printf("  g=%2d  %s\n", g, memcmp(buf, ref, sizeof buf) ? "DIFFERS from memmove" : "identical to memmove");
    }

    printf("\n== StrCatW with an unterminated DESTINATION running into NOACCESS ==\n");
    {
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        wchar_t* d = (wchar_t*)(b + 0x2000) - 8;
        for (int i = 0; i < 8; ++i) d[i] = L'x';
        int f = 0;
        __try { Cat(d, L"abc"); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("  -> %s; the 8 dst chars are now ", f ? "FAULT" : "returned"); show(L"", d, 8);
    }
    return 0;
}
