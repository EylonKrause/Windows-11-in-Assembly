/* changes/284-strstriw/probes/emptyneedle.c
 *
 * What does an empty needle do?  The two exports disagree.
 *
 * probes/contract.c asked this and got "NULL", over the haystack "abcXYZabc". That answer was right
 * about that haystack and wrong about the rule, the SAME defect class this family keeps producing,
 * a corpus that could not express the case. "abcXYZabc" contains no code unit that matches a NUL, and
 * the empty needle's first code unit IS the terminator, so a search for it finds nothing and returns
 * NULL for a reason that has nothing to do with the needle being empty.
 *
 * The live-substitution gate found it: 96 of 30000 cases differed, every one with an empty needle,
 * and every one a case where the generated haystack happened to contain a soft hyphen, one of the
 * 3320 code units that match a NUL (change 282's foldnul.c). There live returned the position of
 * that character while our code returned NULL.
 *
 * So the question is asked properly here: over a haystack that DOES contain a NUL-matching code unit,
 * at a known position, and asked of both exports, because change 283 measured StrRStrIW refusing an
 * empty needle outright, and if the two really differ then inheriting either answer is a mistake.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F2)(PCWSTR, PCWSTR);
typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);

static F2 sstr;
static F3 rstr;

#define SHY  0x00AD     /* matches a NUL */
#define ZWSP 0x200B     /* ignorable, but does NOT match a NUL */

static long a2(const wchar_t* s, const wchar_t* n)
{ PCWSTR p = sstr(s, n); return p ? (long)((const char*)p - (const char*)s) : -1; }
static long a3(const wchar_t* s, const wchar_t* e, const wchar_t* n)
{ PCWSTR p = rstr(s, e, n); return p ? (long)((const char*)p - (const char*)s) : -1; }

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    static wchar_t h[32];
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    sstr = (F2)GetProcAddress(hs, "StrStrIW");
    rstr = (F3)GetProcAddress(hs, "StrRStrIW");
    if (!sstr || !rstr) { printf("resolve failed\n"); return 2; }

    printf("== the empty needle, over haystacks that DO and DO NOT contain a NUL-matching unit ==\n");
    printf("   (BYTE offsets; -1 = not found)\n\n");

    printf("-- 1. plain letters only: no code unit matches a NUL\n");
    {
        for (k = 0; k < 32; ++k) h[k] = L'W';
        for (k = 0; k < 9; ++k) h[k] = (wchar_t)(L'a' + k);
        h[9] = 0;
        printf("   StrStrIW (\"abcdefghi\", \"\")            -> %ld\n", a2(h, L""));
        printf("   StrRStrIW(\"abcdefghi\", end=+9, \"\")    -> %ld\n", a3(h, h + 9, L""));
        printf("   StrRStrIW(\"abcdefghi\", end=+20, \"\")   -> %ld\n", a3(h, h + 20, L""));
    }

    printf("\n-- 2. a SOFT HYPHEN at index 4, which matches a NUL\n");
    {
        for (k = 0; k < 32; ++k) h[k] = L'W';
        for (k = 0; k < 9; ++k) h[k] = (wchar_t)(L'a' + k);
        h[4] = SHY;
        h[9] = 0;
        printf("   StrStrIW  -> %ld    (8 = index 4, the soft hyphen)\n", a2(h, L""));
        printf("   StrRStrIW(end=+9)  -> %ld\n", a3(h, h + 9, L""));
        printf("   StrRStrIW(end=+20) -> %ld\n", a3(h, h + 20, L""));
    }

    printf("\n-- 3. soft hyphens at 2 and 6: which one comes back?\n");
    {
        for (k = 0; k < 32; ++k) h[k] = L'W';
        for (k = 0; k < 9; ++k) h[k] = (wchar_t)(L'a' + k);
        h[2] = SHY; h[6] = SHY;
        h[9] = 0;
        printf("   StrStrIW  -> %ld    (4 = the FIRST one)\n", a2(h, L""));
        printf("   StrRStrIW(end=+9)  -> %ld    (12 would be the LAST one)\n", a3(h, h + 9, L""));
    }

    printf("\n-- 4. a soft hyphen at index 0\n");
    {
        for (k = 0; k < 32; ++k) h[k] = L'W';
        h[0] = SHY; h[1] = L'b'; h[2] = L'c'; h[3] = 0;
        printf("   StrStrIW  -> %ld\n", a2(h, L""));
        printf("   StrRStrIW(end=+3) -> %ld\n", a3(h, h + 3, L""));
    }

    printf("\n-- 5. an ignorable that does NOT match a NUL (zero width space)\n");
    {
        for (k = 0; k < 32; ++k) h[k] = L'W';
        for (k = 0; k < 9; ++k) h[k] = (wchar_t)(L'a' + k);
        h[4] = ZWSP;
        h[9] = 0;
        printf("   StrStrIW  -> %ld    (-1 confirms it is the NUL relation, not ignorability)\n",
               a2(h, L""));
    }

    printf("\n-- 6. an empty haystack, and an empty needle\n");
    {
        static wchar_t e[4];
        e[0] = 0; e[1] = SHY; e[2] = 0;
        printf("   StrStrIW (\"\", \"\")          -> %ld\n", a2(e, L""));
        printf("   StrStrIW (\"\", \"a\")         -> %ld\n", a2(e, L"a"));
        printf("   StrRStrIW(\"\", end=+1, \"\")  -> %ld\n", a3(e, e + 1, L""));
    }

    printf("\n-- 7. does the empty-needle search stop at the terminator, or match it?\n");
    {
        for (k = 0; k < 32; ++k) h[k] = L'W';
        h[0] = L'a'; h[1] = L'b'; h[2] = 0;          /* the terminator itself is a NUL */
        printf("   StrStrIW (\"ab\", \"\")        -> %ld    (-1 = the terminator is not a match)\n",
               a2(h, L""));
        h[1] = SHY;
        printf("   StrStrIW ({a,SHY}, \"\")     -> %ld    (2 = the soft hyphen at index 1)\n",
               a2(h, L""));
    }

    return 0;
}
