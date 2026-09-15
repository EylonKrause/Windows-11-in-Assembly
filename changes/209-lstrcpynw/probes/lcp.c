/* changes/209-lstrcpynw/probes/lcp.c
   Pin down kernelbase!lstrcpynW before writing any assembly.

   Why: 3.37 GB/s to copy 4000 wide characters. A bounded copy that stops at a NUL should run at
   memory speed; 3.37 GB/s is a per-character loop. And unlike the two shlwapi routines the survey
   just ruled out, this one involves no case folding and no collation, so there is no semantic reason
   for it to be slow.

   What has to be settled:
     * exactly how many characters are copied for a given iMaxLength, and where the NUL lands;
     * iMaxLength == 0, 1, and negative;
     * the return value: the destination, or something else;
     * whether the destination is padded (strncpy-style) or just terminated;
     * NULL source / NULL destination;
     * overlap;
     * AND THE BIG ONE: MSDN says lstrcpyn catches exceptions and returns NULL on failure. If it
       really does, a faulting source is part of the contract and a plain copy cannot reproduce it.
       That is tested last, against a PAGE_NOACCESS guard, because it is the difference between a
       viable target and an SEH-wrapped one.                                                       */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*, int);
static FN cpn;

#define PW ((wchar_t)0x2A2A)
#define DSZ 32

static void show(const wchar_t* src, int n, const char* tag){
    wchar_t d[DSZ];
    for (int i = 0; i < DSZ; ++i) d[i] = PW;
    wchar_t* r = cpn(d, src, n);
    printf("  %-30s n=%-5d ret=%-7s buf=[", tag, n, r == d ? "dst" : (r ? "other" : "NULL"));
    for (int i = 0; i < 16; ++i) {
        wchar_t c = d[i];
        putchar(c == 0 ? '.' : (c == PW ? '-' : (c < 128 ? (char)c : '?')));
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    cpn = (FN)GetProcAddress(h, "lstrcpynW");
    if (!cpn) { h = LoadLibraryW(L"kernel32.dll"); cpn = (FN)GetProcAddress(h, "lstrcpynW"); }
    if (!cpn) { printf("no lstrcpynW\n"); return 1; }
    printf("lstrcpynW = %p\n\n", (void*)cpn);

    printf("=== how many characters, and where does the NUL land? ('.'=NUL '-'=untouched) ===\n");
    show(L"abcdefgh", 16, "src 8, n 16 (room to spare)");
    show(L"abcdefgh",  9, "src 8, n 9  (exact fit)");
    show(L"abcdefgh",  8, "src 8, n 8  (one short)");
    show(L"abcdefgh",  5, "src 8, n 5");
    show(L"abcdefgh",  2, "src 8, n 2");
    show(L"abcdefgh",  1, "src 8, n 1");
    show(L"abcdefgh",  0, "src 8, n 0");
    show(L"abcdefgh", -1, "src 8, n -1");
    show(L"abcdefgh", -1000, "src 8, n -1000");
    show(L"",         4, "empty source");

    printf("\n=== is the destination PADDED (strncpy style) or just terminated? ===\n");
    {
        wchar_t d[DSZ];
        for (int i = 0; i < DSZ; ++i) d[i] = PW;
        cpn(d, L"ab", 10);
        printf("  after copying \"ab\" with n=10, cells 0..9: ");
        for (int i = 0; i < 10; ++i) printf("%s", d[i] == 0 ? "." : (d[i] == PW ? "-" : "x"));
        printf("   (\"..-------\" = terminated only, \"..........\" = padded)\n");
    }

    printf("\n=== NULL arguments ===\n");
    {
        wchar_t d[DSZ];
        for (int i = 0; i < DSZ; ++i) d[i] = PW;
        wchar_t* r = cpn(d, NULL, 8);
        printf("  NULL source:      ret=%s  dst[0]=%04X\n",
               r == d ? "dst" : (r ? "other" : "NULL"), (unsigned)d[0]);
        r = cpn(NULL, L"abc", 8);
        printf("  NULL destination: ret=%s\n", r ? "non-NULL" : "NULL");
        r = cpn(NULL, NULL, 8);
        printf("  both NULL:        ret=%s\n", r ? "non-NULL" : "NULL");
    }

    printf("\n=== THE DECIDING TEST: does it swallow a faulting source? ===\n");
    printf("MSDN says lstrcpyn catches exceptions and returns NULL. If so, a source that runs into\n");
    printf("an unmapped page is part of the contract and a plain vectorised copy cannot reproduce it.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        /* an UNTERMINATED source ending exactly at the guard page: reading past it must fault */
        wchar_t* s = (wchar_t*)((base + pg) - 8 * sizeof(wchar_t));
        for (int i = 0; i < 8; ++i) s[i] = L'a' + i;      /* no NUL anywhere */
        static wchar_t d[64];
        for (int i = 0; i < 64; ++i) d[i] = PW;

        printf("  calling with an 8-char unterminated source at a guard page, n=32 ...\n");
        wchar_t* r = cpn(d, s, 32);
        printf("  SURVIVED. ret=%s, dst=[", r == d ? "dst" : (r ? "other" : "NULL"));
        for (int i = 0; i < 12; ++i) {
            wchar_t c = d[i];
            putchar(c == 0 ? '.' : (c == PW ? '-' : (c < 128 ? (char)c : '?')));
        }
        printf("]\n");
        printf("  => it DOES swallow the fault, so SEH is part of the contract.\n");
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}
