/* changes/283-strrstriw/probes/contract.c
 *
 * Is the substring search a per-character loop, or a collation compare?
 *
 * Changes 281 and 282 landed the character searches by owning shlwapi's match relation: locale
 * invariant, decided one character at a time (probes/context.c, 200000 random strings, 0
 * context-dependent matches), symmetric but INTRANSITIVE. A substring search over that relation
 * would be the obvious next thing (compare needle[k] against hay[i+k] for every k) and it is
 * exactly the assumption that has to be checked before a line is written.
 *
 * Because there is a completely different implementation it could have, and one observable
 * consequence tells them apart:
 *
 *     a per-character loop requires the match to be the same length as the needle.
 *     a collation compare does not. CompareStringW gives ignorable characters zero weight, so
 *     "ab<SOFT HYPHEN>c" and "abc" compare EQUAL, and a substring search built on it would find a
 *     three-character needle inside a four-character span.
 *
 * Change 281 measured that the ignorables are real: 3237 of them, all matching each other and
 * nothing else, the largest set in the relation. If they are SKIPPED here rather than matched, then
 * StrRStrIW is doing collation over spans and this change cannot reproduce it with a per-character
 * loop; it would park the way changes 274 and 276 did.
 *
 * So that question is asked first, and the rest of the shape after it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);
typedef PCWSTR (WINAPI *F2)(PCWSTR, PCWSTR);

static F3 rstr;
static F2 sstr;

static int where3(const wchar_t* s, int n, const wchar_t* need)
{
    PCWSTR p = rstr(s, s + n, need);
    return p ? (int)(p - s) : -1;
}
static int where2(const wchar_t* s, const wchar_t* need)
{
    PCWSTR p = sstr(s, need);
    return p ? (int)(p - s) : -1;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");

    setvbuf(stdout, NULL, _IONBF, 0);
    rstr = (F3)GetProcAddress(hs, "StrRStrIW");
    sstr = (F2)GetProcAddress(hs, "StrStrIW");
    if (!rstr || !sstr) { printf("resolve failed\n"); return 1; }

    printf("== 1. THE DECIDING QUESTION: are ignorable characters SKIPPED or MATCHED? ==\n");
    {
        static wchar_t h1[] = { L'a', L'b', 0x00AD, L'c', L'd', 0 };   /* ab<SHY>cd */
        static wchar_t h2[] = { L'a', L'b', L'c', L'd', 0 };
        printf("   haystack \"ab<SOFT HYPHEN>cd\", needle \"abc\" -> %d\n", where3(h1, 5, L"abc"));
        printf("   haystack \"abcd\",              needle \"abc\" -> %d   (a control)\n",
               where3(h2, 4, L"abc"));
        printf("   haystack \"ab<SOFT HYPHEN>cd\", needle \"ab\"  -> %d\n", where3(h1, 5, L"ab"));
        printf("   haystack \"ab<SOFT HYPHEN>cd\", needle \"cd\"  -> %d\n", where3(h1, 5, L"cd"));
        printf("\n   If \"abc\" is found at 0 in the FIRST line, the soft hyphen was SKIPPED and this\n");
        printf("   is a collation compare over spans -- a per-character loop cannot reproduce it.\n");
        printf("   If it is -1, the search is position-by-position and this change is writable.\n");
    }

    printf("\n== 2. does a needle character match an ignorable in the haystack? ==\n");
    {
        static wchar_t h[] = { L'a', 0x00AD, L'c', 0 };
        static wchar_t n1[] = { L'a', 0x200B, L'c', 0 };   /* another ignorable in the needle */
        printf("   haystack \"a<SHY>c\", needle \"a<ZWSP>c\" -> %d   (the two ignorables match\n"
               "                                            each other in change 281's relation)\n",
               where3(h, 3, n1));
        printf("   haystack \"a<SHY>c\", needle \"abc\"      -> %d\n", where3(h, 3, L"abc"));
    }

    printf("\n== 3. the intransitive triple, inside a substring ==\n");
    {
        /* U+D7B0 matches U+D7A2 and U+D7B1 matches U+D7A2, but D7B0 does not match D7B1 */
        static wchar_t h[] = { L'x', 0xD7A2, L'y', 0 };
        static wchar_t n1[] = { L'x', 0xD7B0, L'y', 0 };
        static wchar_t n2[] = { L'x', 0xD7B1, L'y', 0 };
        static wchar_t h2[] = { L'x', 0xD7B0, L'y', 0 };
        static wchar_t n3[] = { L'x', 0xD7B1, L'y', 0 };
        printf("   \"x<D7A2>y\" vs needle \"x<D7B0>y\" -> %d   (they match: expect 0)\n",
               where3(h, 3, n1));
        printf("   \"x<D7A2>y\" vs needle \"x<D7B1>y\" -> %d   (they match: expect 0)\n",
               where3(h, 3, n2));
        printf("   \"x<D7B0>y\" vs needle \"x<D7B1>y\" -> %d   (they do NOT: expect -1)\n",
               where3(h2, 3, n3));
    }

    printf("\n== 4. the shape ==\n");
    {
        static wchar_t t[] = L"abcXYZabc";
        printf("   StrRStrIW(t, t+9, \"abc\")  -> %d   (6: the LAST of two)\n", where3(t, 9, L"abc"));
        printf("   StrRStrIW(t, t+8, \"abc\")  -> %d   (0: the match at 6 does not FIT in the range)\n",
               where3(t, 8, L"abc"));
        printf("   StrRStrIW(t, t+9, \"ABC\")  -> %d   (case-insensitive)\n", where3(t, 9, L"ABC"));
        printf("   StrRStrIW(t, t+9, \"\")     -> %d   (an empty needle)\n", where3(t, 9, L""));
        printf("   StrRStrIW(t, t,   \"abc\")  -> %d   (an empty range)\n", where3(t, 0, L"abc"));
        printf("   StrRStrIW(t, t+2, \"abc\")  -> %d   (a range shorter than the needle)\n",
               where3(t, 2, L"abc"));
        printf("   StrStrIW (t, \"abc\")       -> %d   (FIRST, for comparison)\n", where2(t, L"abc"));
        printf("   StrStrIW (t, \"\")          -> %d\n", where2(t, L""));
    }

    printf("\n== 5. does the needle stop at ITS terminator, and the haystack at the range? ==\n");
    {
        static wchar_t h[8];
        static wchar_t n[4];
        int i;
        for (i = 0; i < 7; ++i) h[i] = (wchar_t)(L'a' + i);
        h[3] = 0;                                   /* a NUL inside the haystack range */
        h[7] = 0;
        n[0] = L'e'; n[1] = L'f'; n[2] = 0; n[3] = 0;
        printf("   haystack \"abc\\0efg\" over 7, needle \"ef\" -> %d   (4 if the NUL is just a\n"
               "                                               character and the range rules)\n",
               where3(h, 7, n));
        n[0] = 0; n[1] = L'e';
        printf("   needle starting with a NUL              -> %d   (an empty needle?)\n",
               where3(h, 7, n));
    }

    printf("\n== 6. NULL arguments ==\n");
    {
        int f1 = 0, f2 = 0;
        PCWSTR r1 = 0, r2 = 0;
        __try { r1 = rstr(0, 0, L"a"); } __except (EXCEPTION_EXECUTE_HANDLER) { f1 = 1; }
        __try { r2 = rstr(L"abc", L"abc" + 3, 0); } __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
        printf("   StrRStrIW(NULL, NULL, \"a\") -> %s\n", f1 ? "FAULTS" : (r1 ? "non-NULL" : "NULL"));
        printf("   StrRStrIW(s, e, NULL)      -> %s\n", f2 ? "FAULTS" : (r2 ? "non-NULL" : "NULL"));
    }
    return 0;
}
