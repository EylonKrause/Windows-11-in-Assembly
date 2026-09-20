/* changes/283-strrstriw/probes/pastnul.c
 *
 * Does the match stop at the terminator?  It does not.
 *
 * This probe exists because change 283 shipped its first draft with a bug, and the corpus written to
 * catch a MUTANT found it instead. The mutant was "the haystack length is not clamped by the needle
 * length" -- dropping `hlen -= nlen`, which sets the highest candidate start to start+hlen rather
 * than start+hlen-nlen. To make that clamp observable at all, the corpus needed a needle that can
 * match ACROSS the terminator, and change 282 had already established that 3238 code units match a
 * NUL. So: a haystack whose only 'q' is its last character, and the needle {'Q', 0x00AD}.
 *
 *     live shlwapi returned the LAST CHARACTER.  Ours and the independent scalar model said NULL.
 *
 * The live export compared 0x00AD against the terminator, and accepted it. Which means the clamp
 * this change was written with is wrong, the "a needle longer than the string returns NULL" line in
 * its RESULTS.md is wrong for such needles, and the mutant was closer to the truth than the code.
 *
 * Both of our sides agreed with each other and both were wrong. That is exactly what the three-way
 * comparison is for -- the live export is one of the three, and it outvoted the pair.
 *
 * So the contract has to be re-derived rather than patched by guess. The questions, in order:
 *
 *   1. how high can a match START -- start+hlen-nlen, start+hlen-1, or higher?
 *   2. how far past the terminator will the comparison run?
 *   3. can a needle LONGER than the whole string match?
 *   4. does `end` bound the start, the comparison, or both?
 *   5. can an EMBEDDED NUL be matched by a NUL-matching needle character?
 *   6. will it read past an unreadable page -- i.e. is the behaviour we must copy also unsafe?
 *
 * Question 6 is asked with a guard page and structured exception handling, because the answer
 * decides whether this change can be bit-exact on every input or only on the inputs a caller can
 * survive.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);
static F3 rstr;

#define SHY 0x00AD      /* SOFT HYPHEN -- matches a NUL, per change 282's foldnul.c */
#define ZWSP 0x200B     /* ZERO WIDTH SPACE -- also matches a NUL */

static long at(const wchar_t* s, const wchar_t* e, const wchar_t* n)
{
    PCWSTR p = rstr(s, e, n);
    return p ? (long)((const char*)p - (const char*)s) : -1;   /* BYTE offset */
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    rstr = (F3)GetProcAddress(hs, "StrRStrIW");
    if (!rstr) { printf("no StrRStrIW\n"); return 2; }

    printf("== pastnul: does the match stop at the terminator? (BYTE offsets) ==\n\n");

    printf("-- 1. how high can a match start? haystack \"zzzq\" (hlen 4, terminator at 4)\n");
    {
        static wchar_t h[16];
        static wchar_t n[8];
        for (k = 0; k < 16; ++k) h[k] = 0;
        h[0] = h[1] = h[2] = L'z'; h[3] = L'q'; h[4] = 0;
        n[0] = L'Q'; n[1] = SHY; n[2] = 0;
        printf("   needle {Q,SHY}   end=+16  -> %ld     (6 = code unit 3 = the LAST character)\n",
               at(h, h + 16, n));
        n[1] = ZWSP;
        printf("   needle {Q,ZWSP}  end=+16  -> %ld     (a different NUL-matching character)\n",
               at(h, h + 16, n));
        n[1] = L'x';
        printf("   needle {Q,x}     end=+16  -> %ld    (x does NOT match a NUL: must be -1)\n",
               at(h, h + 16, n));
    }

    printf("\n-- 2. how far past the terminator does the comparison run?\n");
    {
        static wchar_t h[32];
        static wchar_t n[16];
        int j;
        for (j = 1; j <= 8; ++j) {
            for (k = 0; k < 32; ++k) h[k] = 0;
            h[0] = h[1] = h[2] = L'z'; h[3] = L'q'; h[4] = 0;
            n[0] = L'Q';
            for (k = 1; k <= j; ++k) n[k] = SHY;
            n[j + 1] = 0;
            printf("   needle Q + %d x SHY (nlen %d) -> %ld\n", j, j + 1, at(h, h + 32, n));
        }
    }

    printf("\n-- 3. can a needle LONGER than the string match?\n");
    {
        static wchar_t h[16];
        static wchar_t n[8];
        for (k = 0; k < 16; ++k) h[k] = 0;
        h[0] = L'q'; h[1] = 0;
        n[0] = L'Q'; n[1] = SHY; n[2] = 0;
        printf("   haystack \"q\" (hlen 1), needle {Q,SHY} (nlen 2) -> %ld\n", at(h, h + 16, n));
        n[2] = SHY; n[3] = 0;
        printf("   haystack \"q\" (hlen 1), needle {Q,SHY,SHY}      -> %ld\n", at(h, h + 16, n));
        for (k = 0; k < 16; ++k) h[k] = 0;
        h[0] = 0;
        n[0] = SHY; n[1] = 0;
        printf("   EMPTY haystack, needle {SHY} (nlen 1)           -> %ld\n", at(h, h + 16, n));
        printf("   EMPTY haystack, needle {SHY}, end = start       -> %ld\n", at(h, h, n));
    }

    printf("\n-- 4. does `end` bound the START, the COMPARISON, or both?\n");
    {
        static wchar_t h[16];
        static wchar_t n[8];
        for (k = 0; k < 16; ++k) h[k] = 0;
        h[0] = h[1] = h[2] = L'z'; h[3] = L'q'; h[4] = 0;
        n[0] = L'Q'; n[1] = SHY; n[2] = 0;
        for (k = 0; k <= 6; ++k)
            printf("   end=+%d -> %ld\n", k, at(h, h + k, n));
    }

    printf("\n-- 5. can an EMBEDDED NUL be matched by a NUL-matching needle character?\n");
    {
        static wchar_t h[16];
        static wchar_t n[8];
        for (k = 0; k < 16; ++k) h[k] = 0;
        h[0] = L'a'; h[1] = L'b'; h[2] = 0; h[3] = L'c'; h[4] = L'd'; h[5] = 0;
        n[0] = L'B'; n[1] = SHY; n[2] = 0;
        printf("   haystack \"ab\\0cd\", needle {B,SHY}     -> %ld   (2 = matched the embedded NUL)\n",
               at(h, h + 16, n));
        n[0] = SHY; n[1] = L'C'; n[2] = 0;
        printf("   haystack \"ab\\0cd\", needle {SHY,C}     -> %ld   (does it see past the NUL?)\n",
               at(h, h + 16, n));
        n[0] = SHY; n[1] = 0;
        printf("   haystack \"ab\\0cd\", needle {SHY}       -> %ld\n", at(h, h + 16, n));
    }

    printf("\n-- 6. will it read past an UNREADABLE page? (the terminator is the last readable\n");
    printf("      code unit, and the needle's tail matches a NUL, so a faithful implementation\n");
    printf("      would have to read across the boundary)\n");
    {
        static wchar_t n[8];
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("   GUARD PAGE SETUP FAILED\n");
        } else {
            wchar_t* h = (wchar_t*)(base + pg) - 4;     /* h[3] is the last readable code unit */
            h[0] = L'z'; h[1] = L'z'; h[2] = L'q'; h[3] = 0;
            n[0] = L'Q'; n[1] = SHY; n[2] = 0;
            printf("   terminator last readable, needle {Q,SHY}: ");
            __try {
                printf("%ld  (no fault)\n", at(h, h + 8, n));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("ACCESS VIOLATION -- the live export reads past the page\n");
            }
            n[0] = SHY; n[1] = 0;
            printf("   terminator last readable, needle {SHY} (nlen 1): ");
            __try {
                printf("%ld  (no fault)\n", at(h, h + 8, n));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("ACCESS VIOLATION\n");
            }
        }
    }

    printf("\n-- 7. and the plain control: an ordinary needle must still stop at the terminator\n");
    {
        static wchar_t h[32];
        for (k = 0; k < 32; ++k) h[k] = 0;
        for (k = 0; k < 8; ++k) h[k] = (wchar_t)(L'a' + k);
        h[4] = 0;                                   /* "abcd" then a NUL, then "fgh" behind it */
        printf("   haystack \"abcd\\0fgh\", needle \"fg\" -> %ld   (must be -1: behind the NUL)\n",
               at(h, h + 32, L"fg"));
        printf("   haystack \"abcd\\0fgh\", needle \"bc\" -> %ld   (must be 2)\n",
               at(h, h + 32, L"bc"));
    }

    return 0;
}
