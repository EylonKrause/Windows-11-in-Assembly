/* changes/283-strrstriw/probes/pastnul2.c
 *
 * What does the export compare against past the terminator, real memory, or a virtual NUL?
 *
 * probes/pastnul.c established that a needle whose tail matches a NUL can match across the
 * terminator: over "zzzq" the needle {q, soft hyphen} is found at the last character. That probe
 * used a static, zero-filled array, so it could not tell two very different rules apart:
 *
 *   (a) the export reads the real memory after the terminator and compares it, and the probe only
 *       saw a match because that memory happened to be zero;
 *   (B) the export treats the string as ENDING at the terminator and compares every remaining
 *       needle character against a NUL, a virtual NUL, never a load.
 *
 * The distinction is not academic. Under (A) an implementation must load those code units, and its
 * answer depends on whatever the caller left there. Under (B) it must NOT load them, and the answer
 * depends only on the needle. The live-substitution gate forced the question: it reuses one buffer
 * across 30000 cases, so the code units after a terminator hold the PREVIOUS case's letters, and
 * the export still reported matches that rule (A) would have rejected.
 *
 * So: put deliberately NON-ZERO data immediately after the terminator and ask again. This probe also
 * checks how far the rule extends, whether it applies to an EMBEDDED NUL as well as the terminating
 * one, and whether the export will touch an unreadable page to answer.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);
static F3 rstr;

#define SHY 0x00AD      /* matches a NUL */

static long at(const wchar_t* s, const wchar_t* e, const wchar_t* n)
{
    PCWSTR p = rstr(s, e, n);
    return p ? (long)((const char*)p - (const char*)s) : -1;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    int k, j;

    setvbuf(stdout, NULL, _IONBF, 0);
    rstr = (F3)GetProcAddress(hs, "StrRStrIW");
    if (!rstr) { printf("no StrRStrIW\n"); return 2; }

    printf("== pastnul2: real memory, or a virtual NUL? (BYTE offsets) ==\n\n");

    printf("-- 1. the deciding experiment: NON-ZERO data right after the terminator\n");
    {
        static wchar_t h[32];
        static wchar_t n[8];
        for (k = 0; k < 32; ++k) h[k] = L'W';      /* every code unit non-zero to begin with */
        h[0] = L'z'; h[1] = L'z'; h[2] = L'z'; h[3] = L'q';
        h[4] = 0;                                  /* the terminator, with 'W' after it */
        n[0] = L'Q'; n[1] = SHY; n[2] = 0;
        printf("   \"zzzq\" + NUL + 'W'...,  needle {Q,SHY}      -> %ld\n", at(h, h + 32, n));
        n[2] = SHY; n[3] = 0;
        printf("   \"zzzq\" + NUL + 'W'...,  needle {Q,SHY,SHY}  -> %ld   <-- (B) if 6, (A) if -1\n",
               at(h, h + 32, n));
        n[3] = SHY; n[4] = 0;
        printf("   \"zzzq\" + NUL + 'W'...,  needle {Q,3 x SHY}  -> %ld\n", at(h, h + 32, n));
        n[1] = L'W'; n[2] = 0;
        printf("   \"zzzq\" + NUL + 'W'...,  needle {Q,W}        -> %ld   (must be -1 under (B))\n",
               at(h, h + 32, n));
    }

    printf("\n-- 2. how far does the virtual run extend?\n");
    {
        static wchar_t h[64];
        static wchar_t n[40];
        for (k = 0; k < 64; ++k) h[k] = L'W';
        h[0] = L'q'; h[1] = 0;
        for (j = 1; j <= 32; j *= 2) {
            n[0] = L'Q';
            for (k = 1; k <= j; ++k) n[k] = SHY;
            n[j + 1] = 0;
            printf("   \"q\" + NUL + 'W'..., needle Q + %2d x SHY (nlen %2d) -> %ld\n",
                   j, j + 1, at(h, h + 64, n));
        }
    }

    printf("\n-- 3. does the same hold at an EMBEDDED NUL, or only at the terminator?\n");
    {
        static wchar_t h[32];
        static wchar_t n[8];
        for (k = 0; k < 32; ++k) h[k] = L'W';
        h[0] = L'a'; h[1] = L'b'; h[2] = 0; h[3] = L'c'; h[4] = L'd'; h[5] = 0;
        n[0] = L'B'; n[1] = SHY; n[2] = SHY; n[3] = 0;
        printf("   \"ab\\0cd\", needle {B,SHY,SHY} -> %ld   (2 means the run past the FIRST NUL\n",
               at(h, h + 32, n));
        printf("                                             is virtual too, ignoring 'c')\n");
        n[0] = L'B'; n[1] = SHY; n[2] = L'C'; n[3] = 0;
        printf("   \"ab\\0cd\", needle {B,SHY,C}   -> %ld   (2 would mean it reads the REAL 'c'\n",
               at(h, h + 32, n));
        printf("                                             after the embedded NUL)\n");
    }

    printf("\n-- 4. will it touch an unreadable page to answer?\n");
    {
        static wchar_t n[8];
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
            printf("   GUARD PAGE SETUP FAILED\n");
        } else {
            /* the terminator IS the last readable code unit: under (B) a needle of Q + many SHY
               must still match without a single load past it; under (A) this faults */
            wchar_t* h = (wchar_t*)(base + pg) - 4;
            h[0] = L'z'; h[1] = L'z'; h[2] = L'q'; h[3] = 0;
            for (j = 1; j <= 4; ++j) {
                n[0] = L'Q';
                for (k = 1; k <= j; ++k) n[k] = SHY;
                n[j + 1] = 0;
                printf("   terminator last readable, needle Q + %d x SHY: ", j);
                __try {
                    printf("%ld\n", at(h, h + 8, n));
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    printf("ACCESS VIOLATION\n");
                }
            }
        }
    }

    return 0;
}
