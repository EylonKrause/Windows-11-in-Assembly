/* changes/284-strstriw/probes/contract.c
 *
 * The contract of shlwapi!StrStrIW, asked from scratch.
 *
 * StrStrIW is the FORWARD sibling of change 283's StrRStrIW and takes two arguments rather than
 * three: PCWSTR StrStrIW(PCWSTR haystack, PCWSTR needle). It is tempting to assume it is 283 with
 * the scan direction reversed and `end` removed. Change 283 is the reason not to.
 *
 * That change shipped TWO wrong drafts, both of which passed a gate, and both errors were about what
 * the terminator means. The rule it finally measured is strange enough that it must be re-measured
 * here rather than inherited:
 *
 *     The string behaves as though the terminator were followed by ENDLESS NULs, and those NULs are
 *     never loaded. 3320 code units match a NUL (change 282), so a needle whose TRAILING characters
 *     all match a NUL can match across the terminator -- over "zzzq" the needle {q, soft hyphen} is
 *     found at the last character, and over the one-character string "q" a TWO-character needle is
 *     found at 0.
 *
 * For a BACKWARD search that rule decides which candidates exist at the top of the range. For a
 * FORWARD search the same rule would decide whether the scan may run past the end of the string at
 * all -- and a forward scan that must consider candidates past the terminator is a different loop
 * from one that may stop there. So the question is asked first, and asked in the shape that can tell
 * a real load from a virtual NUL: with NON-ZERO data written after the terminator.
 *
 * Questions, in order:
 *   1. is the comparison PER CHARACTER over change 281's relation, or a collation over spans?
 *   2. does it return the FIRST match?
 *   3. the virtual NUL: can a needle match across the terminator, and is real memory read there?
 *   4. can a needle LONGER than the whole string match?
 *   5. what do an empty needle, an empty string and NULL arguments give?
 *   6. does an EMBEDDED NUL end the search, and can a NUL-matching needle character match it?
 *   7. will it read past an unreadable page?
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F2)(PCWSTR, PCWSTR);
static F2 sstr;

#define SHY  0x00AD     /* SOFT HYPHEN -- matches a NUL (change 282's foldnul.c) */
#define ZWSP 0x200B     /* ZERO WIDTH SPACE -- ignorable, but does NOT match a NUL */

static long at(const wchar_t* s, const wchar_t* n)
{
    PCWSTR p = sstr(s, n);
    return p ? (long)((const char*)p - (const char*)s) : -1;   /* BYTE offset */
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    int k, j;

    setvbuf(stdout, NULL, _IONBF, 0);
    sstr = (F2)GetProcAddress(hs, "StrStrIW");
    if (!sstr) { printf("no StrStrIW\n"); return 2; }

    printf("== StrStrIW contract (BYTE offsets; -1 = not found) ==\n\n");

    printf("-- 1. per character, or a collation over spans?\n");
    {
        static wchar_t h1[] = { L'a', L'b', SHY, L'c', L'd', 0 };   /* ab<SHY>cd */
        printf("   \"ab<SHY>cd\" contains \"abc\"      -> %ld   (-1 means PER CHARACTER)\n",
               at(h1, L"abc"));
        printf("   \"ab<SHY>cd\" contains \"ab\"       -> %ld\n", at(h1, L"ab"));
        printf("   \"ab<SHY>cd\" contains \"cd\"       -> %ld\n", at(h1, L"cd"));
        {
            static wchar_t n3[] = { L'a', L'b', ZWSP, L'c', 0 };
            printf("   \"ab<SHY>cd\" contains {a,b,ZWSP,c} -> %ld   (ignorable vs ignorable)\n",
                   at(h1, n3));
        }
    }

    printf("\n-- 1b. the intransitive triple still holds inside a substring\n");
    {
        static wchar_t h2[] = { L'x', 0xD7A2, L'y', 0 };
        static wchar_t h3[] = { L'x', 0xD7B0, L'y', 0 };
        static wchar_t n1[] = { L'x', 0xD7B0, L'y', 0 };
        static wchar_t n2[] = { L'x', 0xD7B1, L'y', 0 };
        printf("   {x,D7A2,y} contains {x,D7B0,y} -> %ld\n", at(h2, n1));
        printf("   {x,D7A2,y} contains {x,D7B1,y} -> %ld\n", at(h2, n2));
        printf("   {x,D7B0,y} contains {x,D7B1,y} -> %ld   (the third leg: expect -1)\n", at(h3, n2));
    }

    printf("\n-- 2. the FIRST match, not the last\n");
    {
        static wchar_t t[] = L"abcXYZabc";
        printf("   \"abcXYZabc\" contains \"ABC\" -> %ld   (0 = first)\n", at(t, L"ABC"));
        printf("   \"abcXYZabc\" contains \"C\"   -> %ld\n", at(t, L"C"));
    }

    printf("\n-- 3. THE DECIDING QUESTION: a virtual NUL, or real memory?\n");
    {
        static wchar_t h[32];
        static wchar_t n[8];
        for (k = 0; k < 32; ++k) h[k] = L'W';          /* every code unit NON-ZERO */
        h[0] = L'z'; h[1] = L'z'; h[2] = L'z'; h[3] = L'q';
        h[4] = 0;                                      /* terminator, with 'W' after it */
        n[0] = L'Q'; n[1] = SHY; n[2] = 0;
        printf("   \"zzzq\" + NUL + 'W'..., needle {Q,SHY}     -> %ld   (6 = across the terminator)\n",
               at(h, n));
        n[2] = SHY; n[3] = 0;
        printf("   \"zzzq\" + NUL + 'W'..., needle {Q,SHY,SHY} -> %ld   (virtual NUL if 6)\n",
               at(h, n));
        n[1] = L'W'; n[2] = 0;
        printf("   \"zzzq\" + NUL + 'W'..., needle {Q,W}       -> %ld   (-1 if the NUL is virtual)\n",
               at(h, n));
        n[0] = SHY; n[1] = 0;
        printf("   \"zzzq\" + NUL + 'W'..., needle {SHY}       -> %ld   (may a match START at the\n",
               at(h, n));
        printf("                                                          terminator? expect -1)\n");
    }

    printf("\n-- 4. a needle LONGER than the whole string\n");
    {
        static wchar_t h[16];
        static wchar_t n[8];
        for (k = 0; k < 16; ++k) h[k] = L'W';
        h[0] = L'q'; h[1] = 0;
        n[0] = L'Q'; n[1] = SHY; n[2] = 0;
        printf("   \"q\" (hlen 1) contains {Q,SHY} (nlen 2)   -> %ld\n", at(h, n));
        n[2] = SHY; n[3] = 0;
        printf("   \"q\" (hlen 1) contains {Q,SHY,SHY}        -> %ld\n", at(h, n));
        printf("   \"q\" contains \"QQ\"                        -> %ld   (expect -1)\n", at(h, L"QQ"));
    }

    printf("\n-- 5. degenerate arguments\n");
    {
        static wchar_t t[] = L"abcXYZabc";
        static wchar_t e[] = L"";
        printf("   \"abcXYZabc\" contains \"\"   -> %ld\n", at(t, L""));
        printf("   \"\" contains \"abc\"         -> %ld\n", at(e, L"abc"));
        printf("   \"\" contains \"\"            -> %ld\n", at(e, L""));
        printf("   NULL haystack             -> %p\n", (void*)sstr(0, L"a"));
        printf("   NULL needle               -> %p\n", (void*)sstr(t, 0));
        printf("   both NULL                 -> %p\n", (void*)sstr(0, 0));
    }

    printf("\n-- 6. an EMBEDDED NUL\n");
    {
        static wchar_t h[32];
        for (k = 0; k < 32; ++k) h[k] = L'W';
        h[0] = L'a'; h[1] = L'b'; h[2] = 0; h[3] = L'c'; h[4] = L'd'; h[5] = 0;
        printf("   \"ab\\0cd\" contains \"CD\"        -> %ld   (-1 = the NUL ends the search)\n",
               at(h, L"CD"));
        printf("   \"ab\\0cd\" contains \"AB\"        -> %ld\n", at(h, L"AB"));
        {
            static wchar_t m1[] = { L'B', SHY, 0 };
            static wchar_t m2[] = { L'B', SHY, SHY, 0 };
            static wchar_t m3[] = { L'B', SHY, L'C', 0 };
            printf("   \"ab\\0cd\" contains {B,SHY}     -> %ld   (2 = matched the embedded NUL)\n",
                   at(h, m1));
            printf("   \"ab\\0cd\" contains {B,SHY,SHY} -> %ld\n", at(h, m2));
            printf("   \"ab\\0cd\" contains {B,SHY,C}   -> %ld   (2 would mean it read the real 'c')\n",
                   at(h, m3));
        }
    }

    printf("\n-- 7. will it read past an unreadable page?\n");
    {
        static wchar_t n[8];
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("   GUARD PAGE SETUP FAILED\n");
        } else {
            wchar_t* h = (wchar_t*)(base + pg) - 4;    /* h[3] is the last readable code unit */
            h[0] = L'z'; h[1] = L'z'; h[2] = L'q'; h[3] = 0;
            for (j = 1; j <= 3; ++j) {
                n[0] = L'Q';
                for (k = 1; k <= j; ++k) n[k] = SHY;
                n[j + 1] = 0;
                printf("   terminator last readable, needle Q + %d x SHY: ", j);
                __try { printf("%ld\n", at(h, n)); }
                __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION\n"); }
            }
            printf("   terminator last readable, needle \"ZZQ\": ");
            __try { printf("%ld\n", at(h, L"ZZQ")); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION\n"); }
        }
    }

    return 0;
}
