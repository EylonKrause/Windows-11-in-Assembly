/* changes/283-strrstriw/probes/bounds.c
 *
 * WHAT DOES `end` ACTUALLY BOUND, AND WHERE DOES THE HAYSTACK STOP?
 *
 * probes/contract.c settled the important thing -- the comparison is PER CHARACTER, not a collation
 * over spans, because "ab<SOFT HYPHEN>cd" does not contain "abc". That makes this change writable
 * on change 281's relation.
 *
 * It also turned up two results that do NOT match its sibling StrRChrIW, and they have to be pinned
 * down before anything is written:
 *
 *     StrRStrIW(t, t+8, "abc") over "abcXYZabc"  ->  6
 *         but a match at 6 occupies indices 6,7,8 and the range is [0,8). It returned a match that
 *         does NOT FIT inside the range, so `end` is not a bound on the whole match.
 *
 *     StrRStrIW(t, t+2, "abc")                   ->  0
 *         a two-character range found a three-character needle. It read past `end`.
 *
 *     haystack "abc\0efg" over 7, needle "ef"    ->  -1
 *         StrRChrIW walks straight through an embedded NUL. This one does not.
 *
 * So `end` looks like a bound on where a match may START, and the haystack looks NUL-terminated.
 * Both of those change what the implementation may read, so both are measured here against a guard
 * page rather than inferred -- change 282's whole page-safety argument rested on its end pointer
 * being literal, and this one is evidently not.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);
static F3 rstr;

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    rstr = (F3)GetProcAddress(hs, "StrRStrIW");
    if (!rstr) { printf("resolve failed\n"); return 1; }

    printf("== 1. is `end` a bound on the START of the match, or on the whole match? ==\n");
    {
        static wchar_t t[] = L"abcXYZabc";          /* matches of \"abc\" at 0 and 6 */
        for (i = 0; i <= 9; ++i) {
            PCWSTR p = rstr(t, t + i, L"abc");
            printf("   end = t+%d -> %d\n", i, p ? (int)(p - t) : -1);
        }
        printf("   (if the answer becomes 6 as soon as end reaches t+7, `end` bounds the START)\n");
    }

    printf("\n== 2. where does the haystack stop? ==\n");
    {
        static wchar_t h[12];
        for (i = 0; i < 11; ++i) h[i] = (wchar_t)(L'a' + i);
        h[11] = 0;
        printf("   no NUL inside: \"abcdefghijk\" over 11, needle \"jk\" -> %d\n",
               rstr(h, h + 11, L"jk") ? (int)(rstr(h, h + 11, L"jk") - h) : -1);
        h[4] = 0;
        printf("   a NUL at 4:    needle \"jk\" -> %d   (-1 means the NUL stops it)\n",
               rstr(h, h + 11, L"jk") ? (int)(rstr(h, h + 11, L"jk") - h) : -1);
        printf("   a NUL at 4:    needle \"bc\" -> %d   (a match BEFORE the NUL)\n",
               rstr(h, h + 11, L"bc") ? (int)(rstr(h, h + 11, L"bc") - h) : -1);
        printf("   a NUL at 4:    needle \"d\"  -> %d\n",
               rstr(h, h + 11, L"d") ? (int)(rstr(h, h + 11, L"d") - h) : -1);
    }

    printf("\n== 3. THE GUARD PAGE: how far past `end` may it read? ==\n");
    GetSystemInfo(&si);
    pg = si.dwPageSize;
    base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
        printf("   guard page setup failed\n");
    } else {
        /* a NUL-terminated string whose terminator is the last readable code unit */
        wchar_t* g = (wchar_t*)(base + pg) - 6;      /* "abcde" + NUL */
        int faulted;
        PCWSTR r;
        for (i = 0; i < 5; ++i) g[i] = (wchar_t)(L'a' + i);
        g[5] = 0;

        faulted = 0; r = 0;
        __try { r = rstr(g, g + 5, L"de"); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   end = start+5 (the whole string)      -> %s%d\n",
               faulted ? "FAULT " : "", faulted ? -1 : (r ? (int)(r - g) : -1));

        faulted = 0; r = 0;
        __try { r = rstr(g, g + 6, L"de"); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   end = start+6 (one past the NUL)      -> %s%d\n",
               faulted ? "FAULT " : "", faulted ? -1 : (r ? (int)(r - g) : -1));

        faulted = 0; r = 0;
        __try { r = rstr(g, g + 64, L"de"); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   end = start+64 (far past the page)    -> %s%d\n",
               faulted ? "FAULT " : "", faulted ? -1 : (r ? (int)(r - g) : -1));
        printf("   (no fault on the last line means the NUL stops it before `end` does)\n");

        /* and a haystack with NO terminator before the page: does `end` alone save it? */
        for (i = 0; i < 6; ++i) g[i] = (wchar_t)(L'a' + i);
        faulted = 0; r = 0;
        __try { r = rstr(g, g + 6, L"zz"); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   NO terminator, end = start+6, no match -> %s%d\n",
               faulted ? "FAULT" : "", faulted ? -1 : (r ? (int)(r - g) : -1));
        printf("   (a FAULT here means it reads past `end` looking for a terminator, and the\n");
        printf("    caller must supply one -- which is a different contract from StrRChrIW)\n");
    }

    printf("\n== 4. a needle longer than the haystack ==\n");
    {
        static wchar_t t[] = L"ab";
        printf("   \"ab\" over 2, needle \"abcdef\" -> %d\n",
               rstr(t, t + 2, L"abcdef") ? (int)(rstr(t, t + 2, L"abcdef") - t) : -1);
    }
    return 0;
}
