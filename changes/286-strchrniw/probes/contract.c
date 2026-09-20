/* changes/286-strchrniw/probes/contract.c
 *
 * What are StrChrNIW's arguments?  Two sources disagree, so neither is trusted.
 *
 * The documented shape is
 *
 *     PWSTR StrChrNIW(PCWSTR lpStart, WCHAR wMatch, UINT cchMax)
 *
 * -- a character and a COUNT. But discovery/charclass_strcmp_2026.c timed it as
 *
 *     nn(A, A + 511, L'#')        labelled "range form"
 *
 * reusing the three-argument typedef from StrRChrIW: a start, an end pointer and a character. Those two
 * readings are incompatible, and the discovery call still produced a number, because a wrong argument
 * order does not fault -- it just measures something else. That number (1655 ns) is the only reason this
 * export is on the list at all, so the first thing to establish is which reading is real.
 *
 * Every one of changes 283, 284 and 285 was wrong somewhere until it measured instead of inheriting:
 * 283 shipped two wrong drafts about the terminator, 284 inherited 283's empty-needle answer and was
 * wrong because the two exports genuinely differ, and 285 found three of its own corpora vacuous. So
 * this probe assumes nothing, not even the argument count.
 *
 * THE METHOD. All three arguments arrive in registers (rcx, rdx, r8), so the same address can be called
 * through either prototype and the answers compared. A distinguishing case is easy to build: put the
 * target character at index 2 of a six-character string and ask with a count of 6 and with an end
 * pointer of start+6. Only one of the two readings can return start+2 for both, and the readings differ
 * sharply when the second argument is small -- a count of 3 bounds the search, while a POINTER of 3 is
 * a wild address below the string.
 *
 * Then, once the shape is known: the relation, the bound's exact meaning, the terminator, and the
 * degenerate arguments.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PWSTR  (WINAPI *F_cnt)(PCWSTR, WCHAR, UINT);      /* the documented shape */
typedef PCWSTR (WINAPI *F_rng)(PCWSTR, PCWSTR, WCHAR);    /* the shape discovery assumed */

static F_cnt as_count;
static F_rng as_range;

static long off(const wchar_t* base, const void* p)
{
    return p ? (long)((const char*)p - (const char*)base) : -1;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    void* fn;
    static wchar_t s[64];
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    fn = (void*)GetProcAddress(hs, "StrChrNIW");
    if (!fn) { printf("no StrChrNIW in this build\n"); return 2; }
    as_count = (F_cnt)fn;
    as_range = (F_rng)fn;

    for (k = 0; k < 64; ++k) s[k] = 0;
    for (k = 0; k < 6; ++k) s[k] = (wchar_t)(L'a' + k);      /* "abcdef" */

    printf("== StrChrNIW: which prototype is real? (BYTE offsets; -1 = NULL) ==\n\n");

    printf("-- 1. the deciding pair, target 'c' at index 2 of \"abcdef\"\n");
    printf("   as (start, 'C', 6)          -> %ld    (4 = index 2, so the COUNT reading holds)\n",
           off(s, as_count(s, L'C', 6)));
    printf("   as (start, start+6, 'C')    -> %ld    (4 would mean the RANGE reading holds)\n",
           off(s, as_range(s, s + 6, L'C')));

    printf("\n-- 2. and with a SMALL second argument, where the two readings diverge hardest\n");
    printf("   as (start, 'C', 2)          -> %ld    (-1 if a count of 2 excludes index 2)\n",
           off(s, as_count(s, L'C', 2)));
    printf("   as (start, 'C', 3)          -> %ld    (4 if a count of 3 includes index 2)\n",
           off(s, as_count(s, L'C', 3)));
    printf("   as (start, 'C', 0)          -> %ld    (-1 for an empty range)\n",
           off(s, as_count(s, L'C', 0)));

    printf("\n-- 3. is it case-insensitive, and is it change 281's relation?\n");
    printf("   (start, 'C', 6)  upper      -> %ld\n", off(s, as_count(s, L'C', 6)));
    printf("   (start, 'c', 6)  lower      -> %ld\n", off(s, as_count(s, L'c', 6)));
    {
        /* the intransitive triple: U+D7A2 matches both U+D7B0 and U+D7B1, which do not match each
           other. If this export uses change 281's relation, all three answers follow that. */
        static wchar_t t[8];
        t[0] = L'x'; t[1] = 0xD7B0; t[2] = L'y'; t[3] = 0;
        printf("   {x,D7B0,y} searched for D7A2 -> %ld   (2 = the triple holds)\n",
               off(t, as_count(t, (WCHAR)0xD7A2, 3)));
        printf("   {x,D7B0,y} searched for D7B1 -> %ld   (-1 = and these two do not match)\n",
               off(t, as_count(t, (WCHAR)0xD7B1, 3)));
        t[1] = 0xD7A2;
        printf("   {x,D7A2,y} searched for D7B0 -> %ld   (2 = symmetric)\n",
               off(t, as_count(t, (WCHAR)0xD7B0, 3)));
    }
    {
        static wchar_t u[8];
        u[0] = L'a'; u[1] = 0x034F; u[2] = L'b'; u[3] = 0;
        printf("   {a,034F,b} searched for 00AD -> %ld   (2 = the ignorable set, as in 285)\n",
               off(u, as_count(u, (WCHAR)0x00AD, 3)));
        u[1] = 0x200B;
        printf("   {a,200B,b} searched for 00AD -> %ld   (-1: U+200B matches only itself)\n",
               off(u, as_count(u, (WCHAR)0x00AD, 3)));
    }

    printf("\n-- 4. does the TERMINATOR stop it before the count does?\n");
    {
        for (k = 0; k < 64; ++k) s[k] = L'W';                /* non-zero past the terminator */
        for (k = 0; k < 4; ++k) s[k] = (wchar_t)(L'a' + k);  /* "abcd" */
        s[4] = 0;
        printf("   \"abcd\" + NUL + 'W'..., search 'W' count 20 -> %ld\n",
               off(s, as_count(s, L'W', 20)));
        printf("      (-1 means the terminator stops it; 10+ means it reads past the NUL)\n");
        printf("   \"abcd\" + NUL + 'W'..., search 'D' count 20 -> %ld   (6)\n",
               off(s, as_count(s, L'D', 20)));
        {
            /* and can the terminator itself be matched, as it can in 283/284? */
            printf("   \"abcd\" + NUL + 'W'..., search NUL count 20 -> %ld\n",
                   off(s, as_count(s, (WCHAR)0, 20)));
            printf("   \"abcd\" + NUL + 'W'..., search SOFT HYPHEN count 20 -> %ld\n",
                   off(s, as_count(s, (WCHAR)0x00AD, 20)));
            printf("      (8 would mean a NUL-matching character matches the terminator)\n");
        }
    }

    printf("\n-- 5. an EMBEDDED NUL\n");
    {
        for (k = 0; k < 64; ++k) s[k] = L'W';
        s[0] = L'a'; s[1] = L'b'; s[2] = 0; s[3] = L'c'; s[4] = L'd'; s[5] = 0;
        printf("   \"ab\\0cd\", search 'D' count 10 -> %ld   (-1 = the NUL stops it)\n",
               off(s, as_count(s, L'D', 10)));
        printf("   \"ab\\0cd\", search 'B' count 10 -> %ld   (2)\n",
               off(s, as_count(s, L'B', 10)));
    }

    printf("\n-- 6. degenerate arguments\n");
    {
        static wchar_t e[2];
        e[0] = 0;
        printf("   empty string, 'a', 5       -> %ld\n", off(e, as_count(e, L'a', 5)));
        printf("   NULL start, 'a', 5         -> %p\n", (void*)as_count(0, L'a', 5));
        static wchar_t t[] = L"abcdef";
        printf("   \"abcdef\", 'a', 1           -> %ld   (0: the first character, count 1)\n",
               off(t, as_count(t, L'a', 1)));
        printf("   \"abcdef\", 'b', 1           -> %ld   (-1: index 1 is outside a count of 1)\n",
               off(t, as_count(t, L'b', 1)));
        printf("   \"abcdef\", 'f', 6           -> %ld   (10: the last character, count exactly 6)\n",
               off(t, as_count(t, L'f', 6)));
        printf("   \"abcdef\", 'f', 5           -> %ld   (-1: a count of 5 stops one short)\n",
               off(t, as_count(t, L'f', 5)));
    }

    printf("\n-- 7. will it read past an UNREADABLE page when the count allows?\n");
    {
        SYSTEM_INFO si;
        unsigned char* base;
        SIZE_T pg;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("   GUARD PAGE SETUP FAILED\n");
        } else {
            wchar_t* h = (wchar_t*)(base + pg) - 4;        /* h[3] is the last readable code unit */
            h[0] = L'a'; h[1] = L'b'; h[2] = L'c'; h[3] = 0;
            printf("   terminated, last readable is the NUL, count 64: ");
            __try { printf("%ld  (no fault: the NUL stopped it)\n", off(h, as_count(h, L'#', 64))); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION\n"); }
            h[3] = L'd';                                   /* NO terminator now */
            printf("   UNTERMINATED, count exactly 4:          ");
            __try { printf("%ld  (no fault: the count stopped it)\n", off(h, as_count(h, L'#', 4))); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION\n"); }
            printf("   UNTERMINATED, count 64:                 ");
            __try { printf("%ld  (no fault?)\n", off(h, as_count(h, L'#', 64))); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            { printf("ACCESS VIOLATION -- the count does NOT bound the reads\n"); }
        }
    }

    return 0;
}
