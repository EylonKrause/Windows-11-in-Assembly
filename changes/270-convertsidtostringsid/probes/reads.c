/* changes/270-convertsidtostringsid/probes/reads.c
 *
 * How much of the SID does it read, and in what order does it refuse?
 *
 * probes/contract.c established that advapi32!ConvertSidToStringSidW and
 * ntdll!RtlConvertSidToUnicodeString produce the same text for every shape asked, and that both
 * refuse a revision other than 1 and a sub-authority count above 15. That is enough to build the
 * formatter. It is NOT enough to build the wrapper, because three things are still unmeasured and
 * each of them is a fault or a wrong answer if guessed:
 *
 *   1. How far does it read? a SID is 8 + 4*count bytes and the count is a byte inside it. a caller
 *      that hands over a SID at the very end of a committed page is entitled to expect the export
 *      to read exactly that many bytes and no more. This is measured with a guard page, the same
 *      way changes 016 and 034 measure their sources: two pages, one committed, the next
 *      PAGE_NOACCESS, and the SID placed so its last byte ends the committed page.
 *
 *   2. What is checked first? With a bad revision and a bad count, which error comes back tells you
 *      the order. More usefully: does a bad revision stop it before it reads the count byte at all?
 *      A SID consisting of ONE readable byte, with everything after it on a no-access page, answers
 *      that -- if the call refuses cleanly, the revision is checked before anything else is touched.
 *
 *   3. Does success touch the last error? a caller that calls this and then reports GetLastError on
 *      an unrelated failure sees whatever this left behind. It is cheap to measure and cheap to
 *      reproduce, and impossible to guess.
 *
 * Everything here runs under __try/__except so that a fault is a RESULT rather than a crash.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

/* two pages: the first committed, the second reserved no-access */
static unsigned char* g_base;
static SIZE_T g_pagesz;

static unsigned char* guarded(SIZE_T n)   /* returns a pointer whose last byte ends the page */
{
    return g_base + g_pagesz - n;
}

static void fill_sid(unsigned char* p, unsigned rev, unsigned long long auth,
                     unsigned n, unsigned long first)
{
    unsigned i;
    p[0] = (unsigned char)rev;
    p[1] = (unsigned char)n;
    for (i = 0; i < 6; ++i) p[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < n; ++i) {
        unsigned long v = first + i;
        p[8 + 4 * i + 0] = (unsigned char)v;
        p[8 + 4 * i + 1] = (unsigned char)(v >> 8);
        p[8 + 4 * i + 2] = (unsigned char)(v >> 16);
        p[8 + 4 * i + 3] = (unsigned char)(v >> 24);
    }
}

static void try_one(const char* what, unsigned char* sid)
{
    LPWSTR out = 0;
    BOOL ok = FALSE;
    DWORD err = 0;
    int faulted = 0;
    __try {
        SetLastError(0);
        ok = ConvertSidToStringSidW((PSID)sid, &out);
        err = GetLastError();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        faulted = 1;
    }
    printf("  %-46s ", what);
    if (faulted)      printf("FAULTED -- it read past the SID\n");
    else if (ok)      { printf("OK   %ls\n", out ? out : L"(null)"); }
    else              printf("refused, err=%lu\n", (unsigned long)err);
    if (out) LocalFree(out);
}

int main(void)
{
    SYSTEM_INFO si;
    unsigned char* p;
    unsigned i;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetSystemInfo(&si);
    g_pagesz = si.dwPageSize;

    g_base = (unsigned char*)VirtualAlloc(0, g_pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!g_base) { printf("reserve failed\n"); return 1; }
    if (!VirtualAlloc(g_base, g_pagesz, MEM_COMMIT, PAGE_READWRITE)) {
        printf("commit failed\n"); return 1;
    }
    printf("== a SID whose LAST BYTE ends a committed page, with no-access after it ==\n");
    printf("   (a fault here means the export read past 8 + 4*count)\n");
    for (i = 0; i <= 15; ++i) {
        char nm[64];
        SIZE_T n = 8 + 4 * (SIZE_T)i;
        p = guarded(n);
        fill_sid(p, 1, 5, i, 1000000000ul);
        wsprintfA(nm, "%u sub-authorities, exactly %Iu bytes", i, n);
        try_one(nm, p);
    }

    printf("\n== and a count byte that is too big: is the count checked before the read? ==\n");
    for (i = 16; i <= 20; ++i) {
        char nm[64];
        /* only 8 + 4*15 bytes are readable; the count byte claims more */
        SIZE_T n = 8 + 4 * 15;
        p = guarded(n);
        fill_sid(p, 1, 5, 15, 1);
        p[1] = (unsigned char)i;
        wsprintfA(nm, "count byte %u, only 68 bytes readable", i);
        try_one(nm, p);
    }

    printf("\n== is the REVISION checked before anything else is touched? ==\n");
    {
        /* exactly ONE readable byte: the revision. Everything after it is no-access. */
        p = guarded(1);
        p[0] = 2;
        try_one("revision 2, and only that ONE byte readable", p);
        p = guarded(1);
        p[0] = 1;
        try_one("revision 1, and only that ONE byte readable", p);
        p = guarded(2);
        p[0] = 1; p[1] = 0;
        try_one("revision 1, count 0, only TWO bytes readable", p);
        p = guarded(8);
        fill_sid(p, 1, 5, 0, 0);
        try_one("revision 1, count 0, the full header readable", p);
    }

    printf("\n== does SUCCESS disturb the last error? ==\n");
    {
        static unsigned char sid[8 + 4 * 5];
        LPWSTR out = 0;
        DWORD before, after;
        fill_sid(sid, 1, 5, 5, 21);
        SetLastError(0xD15EA5E);
        before = GetLastError();
        if (ConvertSidToStringSidW((PSID)sid, &out)) {
            after = GetLastError();
            printf("  last error before %08lX, after a SUCCESSFUL call %08lX -- %s\n",
                   (unsigned long)before, (unsigned long)after,
                   before == after ? "UNTOUCHED" : "CHANGED");
            LocalFree(out);
        } else {
            printf("  the control case failed, err=%lu\n", (unsigned long)GetLastError());
        }
    }

    printf("\n== both arguments NULL, and an unreadable OUT pointer ==\n");
    {
        LPWSTR* bad = (LPWSTR*)(g_base + g_pagesz);      /* no-access */
        static unsigned char sid[8 + 4];
        BOOL ok = FALSE;
        int faulted = 0;
        fill_sid(sid, 1, 5, 1, 7);
        SetLastError(0);
        ok = ConvertSidToStringSidW(0, 0);
        printf("  both NULL          -> %s err=%lu\n", ok ? "OK" : "NO",
               (unsigned long)GetLastError());
        __try {
            SetLastError(0);
            ok = ConvertSidToStringSidW((PSID)sid, bad);
            printf("  a no-access OUT    -> %s err=%lu  (it did not fault)\n", ok ? "OK" : "NO",
                   (unsigned long)GetLastError());
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("  a no-access OUT    -> FAULTED (no SEH inside the export)\n");
        }
    }
    return 0;
}
