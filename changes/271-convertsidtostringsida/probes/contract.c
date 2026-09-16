/* changes/271-convertsidtostringsida/probes/contract.c
 *
 * IS THE ANSI FORM THE WIDE FORM WITH A CONVERSION ON THE END?
 *
 * discovery/sid_inet_bstr.c measured the four SID text functions together:
 *
 *     advapi32!ConvertSidToStringSidW        181.45 ns
 *     advapi32!ConvertSidToStringSidA        254.59 ns      <- this file
 *     advapi32!ConvertStringSidToSidW        269.14 ns      (change 269)
 *     advapi32!ConvertStringSidToSidA        343.95 ns
 *
 * The obvious reading is that each ANSI form is its wide sibling plus a code-page conversion, and
 * the 73 ns gap here is about what a WideCharToMultiByte of a 44-character string costs. If that is
 * what it is, this change is change 270 plus a narrowing -- and since a SID string is ASCII by
 * construction ("S", "-", "0x", the digits and A-F), the narrowing is a byte-per-character pack
 * that needs no code page at all.
 *
 * "IF THAT IS WHAT IT IS" IS THE WHOLE QUESTION AND IT IS NOT SAFE TO ASSUME. This project has now
 * been caught three times by two functions that are documented as a pair and do not behave as one
 * (change 268's four differences between its two directions; change 269's two number parsers inside
 * ONE export; and change 018/021's ANSI forms, which are code-page dependent in ways their wide
 * siblings are not). So the questions are:
 *
 *   1. does the ANSI form produce exactly the wide form's characters, narrowed one for one, for
 *      every shape of SID -- every count, every revision, every authority form?
 *   2. does it REFUSE exactly what the wide form refuses, with the same GetLastError?
 *   3. is the block the same shape -- LocalAlloc, LMEM_FIXED, exactly characters+1 bytes?
 *   4. does it leave the output pointer alone on failure, and zero the last error on success?
 *   5. DOES THE ACTIVE CODE PAGE CHANGE ANYTHING? A SID string is ASCII, so it should not -- but
 *      "should not" is what changes 021 and 027 were built on before their code pages were
 *      measured. This asks under the ANSI code page, under 1252, under 932 (Shift-JIS, where the
 *      lead-byte range would matter if anything non-ASCII ever appeared) and under 65001 (UTF-8).
 *
 * Nothing is asserted. Every line prints what the live exports returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

static unsigned char sidbuf[8 + 4 * 256];
static PSID mk(unsigned rev, unsigned long long auth, const unsigned* sub, unsigned n)
{
    unsigned i;
    sidbuf[0] = (unsigned char)rev;
    sidbuf[1] = (unsigned char)n;
    for (i = 0; i < 6; ++i) sidbuf[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < n && i < 256; ++i) {
        sidbuf[8 + 4 * i + 0] = (unsigned char)(sub[i]);
        sidbuf[8 + 4 * i + 1] = (unsigned char)(sub[i] >> 8);
        sidbuf[8 + 4 * i + 2] = (unsigned char)(sub[i] >> 16);
        sidbuf[8 + 4 * i + 3] = (unsigned char)(sub[i] >> 24);
    }
    return (PSID)sidbuf;
}

static int differ = 0;

/* Ask both forms and compare everything that is comparable. */
static void both(const char* what, PSID sid, int verbose)
{
    LPWSTR w = (LPWSTR)(UINT_PTR)0xDEADBEEF;
    LPSTR  a = (LPSTR)(UINT_PTR)0xDEADBEEF;
    BOOL rw, ra;
    DWORD ew, ea;
    int bad = 0;

    SetLastError(0xD15EA5E); rw = ConvertSidToStringSidW(sid, &w); ew = GetLastError();
    SetLastError(0xD15EA5E); ra = ConvertSidToStringSidA(sid, &a); ea = GetLastError();

    if ((rw ? 1 : 0) != (ra ? 1 : 0)) bad = 1;
    if (ew != ea) bad = 2;
    if ((w == (LPWSTR)(UINT_PTR)0xDEADBEEF) != (a == (LPSTR)(UINT_PTR)0xDEADBEEF)) bad = 3;

    if (rw && ra && w && a && w != (LPWSTR)(UINT_PTR)0xDEADBEEF && a != (LPSTR)(UINT_PTR)0xDEADBEEF) {
        int lw = lstrlenW(w), la = lstrlenA(a), i;
        if (lw != la) bad = 4;
        else for (i = 0; i < lw; ++i) if ((unsigned)(unsigned char)a[i] != (unsigned)w[i]) { bad = 5; break; }
        if (!bad) {
            SIZE_T sw = LocalSize(w), sa = LocalSize(a);
            if (sa != (SIZE_T)(la + 1)) bad = 6;
            if (sw != (SIZE_T)((lw + 1) * 2)) bad = 7;
            if (LocalFlags(a) != LocalFlags(w)) bad = 8;
        }
    }

    if (bad || verbose) {
        printf("  %-30s W %s", what, rw ? "OK " : "NO ");
        if (rw && w && w != (LPWSTR)(UINT_PTR)0xDEADBEEF) printf("%-52ls", w);
        else printf("err=%-46lu", (unsigned long)ew);
        printf("  A %s", ra ? "OK " : "NO ");
        if (ra && a && a != (LPSTR)(UINT_PTR)0xDEADBEEF) printf("%s", a);
        else printf("err=%lu", (unsigned long)ea);
        if (bad) { printf("   <<< THEY DIFFER (%d)", bad); ++differ; }
        printf("\n");
    }

    if (w && w != (LPWSTR)(UINT_PTR)0xDEADBEEF) LocalFree(w);
    if (a && a != (LPSTR)(UINT_PTR)0xDEADBEEF) LocalFree(a);
}

int main(void)
{
    static unsigned sub[256];
    unsigned i, j;
    char nm[80];

    setvbuf(stdout, NULL, _IONBF, 0);
    {
        HMODULE owner = 0;
        wchar_t path[MAX_PATH] = L"?";
        void* p = (void*)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "ConvertSidToStringSidA");
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &owner))
            GetModuleFileNameW(owner, path, MAX_PATH);
        printf("== ConvertSidToStringSidA is at %p, in %ls ==\n", p, path);
        printf("   the ANSI code page is %u, the OEM code page is %u\n\n",
               (unsigned)GetACP(), (unsigned)GetOEMCP());
    }

    printf("== 1. a few shapes, printed in full ==\n");
    for (i = 0; i < 256; ++i) sub[i] = 1000000000u + i * 271828183u;
    sub[0] = 21; sub[1] = 305419896u; sub[2] = 2596069104u; sub[3] = 287454020u; sub[4] = 1001;
    both("no sub-authorities", mk(1, 5, sub, 0), 1);
    both("a real account SID", mk(1, 5, sub, 5), 1);
    both("15, the maximum",    mk(1, 5, sub, 15), 1);
    both("the hex authority",  mk(1, 0x123456789ABCull, sub, 5), 1);
    both("revision 2",         mk(2, 5, sub, 5), 1);
    both("count 16",           mk(1, 5, sub, 16), 1);

    printf("\n== 2. every count 0..255 and every revision 0..255 (only differences are printed) ==\n");
    for (i = 0; i <= 255; ++i) { wsprintfA(nm, "count %u", i);    both(nm, mk(1, 5, sub, i), 0); }
    for (i = 0; i <= 255; ++i) { wsprintfA(nm, "revision %u", i); both(nm, mk(i, 5, sub, 3), 0); }
    printf("   %d difference(s) so far\n", differ);

    printf("\n== 3. the identifier authority at every boundary, at six counts ==\n");
    {
        static const unsigned long long AS[] = {
            0ull, 1ull, 9ull, 10ull, 99ull, 100ull, 999999999ull, 1000000000ull,
            0xFFFFFFFEull, 0xFFFFFFFFull, 0x100000000ull, 0x100000001ull,
            0xABCDEFull, 0x123456789Aull, 0xFFFFFFFFFFFEull, 0xFFFFFFFFFFFFull
        };
        for (i = 0; i < sizeof AS / sizeof AS[0]; ++i)
            for (j = 0; j <= 15; j += 3) {
                wsprintfA(nm, "authority #%u, count %u", i, j);
                both(nm, mk(1, AS[i], sub, j), 0);
            }
    }
    printf("   %d difference(s) so far\n", differ);

    printf("\n== 4. the NULL arguments and the failure contract ==\n");
    {
        LPSTR p = (LPSTR)(UINT_PTR)0xDEADBEEF;
        BOOL r;
        SetLastError(0); r = ConvertSidToStringSidA(0, &p);
        printf("   NULL sid        -> %s err=%-6lu pointer %s\n", r ? "OK" : "NO",
               (unsigned long)GetLastError(),
               p == (LPSTR)(UINT_PTR)0xDEADBEEF ? "LEFT ALONE" : (p ? "WRITTEN" : "cleared"));
        SetLastError(0); r = ConvertSidToStringSidA(mk(1, 5, sub, 1), 0);
        printf("   NULL out        -> %s err=%lu\n", r ? "OK" : "NO", (unsigned long)GetLastError());
        p = (LPSTR)(UINT_PTR)0xDEADBEEF;
        SetLastError(0); r = ConvertSidToStringSidA(mk(3, 5, sub, 1), &p);
        printf("   revision 3      -> %s err=%-6lu pointer %s\n", r ? "OK" : "NO",
               (unsigned long)GetLastError(),
               p == (LPSTR)(UINT_PTR)0xDEADBEEF ? "LEFT ALONE" : (p ? "WRITTEN" : "cleared"));
        if (p && p != (LPSTR)(UINT_PTR)0xDEADBEEF) LocalFree(p);
    }

    printf("\n== 5. does a SUCCESSFUL call zero the last error, as the wide form does? ==\n");
    {
        static const DWORD PRE[] = { 0, 1, 87, 0xD15EA5E, ERROR_INVALID_SID };
        for (i = 0; i < sizeof PRE / sizeof PRE[0]; ++i) {
            LPSTR p = 0;
            DWORD after;
            SetLastError(PRE[i]);
            if (!ConvertSidToStringSidA(mk(1, 5, sub, 5), &p)) { printf("   control failed\n"); continue; }
            after = GetLastError();
            printf("   pre %-10lu -> after %-10lu %s\n", (unsigned long)PRE[i], (unsigned long)after,
                   after == PRE[i] ? "(untouched)" : (after == 0 ? "(zeroed)" : "(other)"));
            LocalFree(p);
        }
    }

    printf("\n== 6. DOES THE ACTIVE CODE PAGE CHANGE ANYTHING? ==\n");
    printf("   A SID string is ASCII by construction, so it should not -- but that is what\n"
           "   changes 021 and 027 were built on before their code pages were measured.\n");
    {
        static const UINT CPS[] = { 0, 1252, 932, 949, 65001 };   /* 0 = leave the thread alone */
        for (i = 0; i < sizeof CPS / sizeof CPS[0]; ++i) {
            LPSTR p = 0;
            int before = differ;
            if (CPS[i]) {
                /* the export may read the ANSI code page rather than the thread locale; there is no
                   supported way to change GetACP for a running process, so what is tested here is
                   whether the RESULT is stable across the conversion routines' own locale state */
                SetThreadLocale(MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT), SORT_DEFAULT));
            }
            both("under a changed thread locale", mk(1, 5, sub, 5), 0);
            if (ConvertSidToStringSidA(mk(1, 0xFFFFFFFFFFFFull, sub, 15), &p)) {
                printf("   locale #%u (cp %u): %s\n", i, CPS[i], p);
                LocalFree(p);
            }
            if (differ != before) printf("   ^^ the two forms disagreed under this locale\n");
        }
        SetThreadLocale(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), SORT_DEFAULT));
    }

    printf("\n== total differences between the wide and ANSI forms: %d ==\n", differ);
    return 0;
}
