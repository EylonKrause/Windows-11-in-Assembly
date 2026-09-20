/* changes/289-widechartomultibyte/probes/contract.c
 *
 * WHAT THE LIVE EXPORT ACTUALLY DOES -- asked, not assumed.
 *
 * The documentation for WideCharToMultiByte says several things about CP_UTF8 that the shipped
 * code does not do, and the disassembly of kernelbase!WideCharToMultiByte (RVA 0x00054A80 in
 * 10.0.26100.9278) says so plainly. This probe asks the running function and prints the answers,
 * so that reference.c is written from proof rather than from MSDN.
 *
 * The questions, in the order the shipped code asks them:
 *   1  which import does the CP_UTF8 path call?              (IAT identity, against GetProcAddress)
 *   2  cchWideChar == 0                                      -> ?
 *   3  cbMultiByte < 0                                       -> ?
 *   4  lpWideCharStr == NULL                                 -> ?
 *   5  lpMultiByteStr == NULL with cbMultiByte != 0          -> ?
 *   6  lpMultiByteStr == lpWideCharStr (exactly)             -> ?  and merely OVERLAPPING?
 *   7  which dwFlags bits are accepted?                      (all 32, one at a time)
 *   8  lpDefaultChar / lpUsedDefaultChar non-NULL            -> error, or used?
 *   9  negative cchWideChar other than -1                    -> ?
 *  10  the measuring mode (cbMultiByte == 0)                 -> value, and is lpMultiByteStr read?
 *  11  a destination that is too small                       -> return, error, and WHAT IS WRITTEN
 *  12  a lone surrogate, with and without WC_ERR_INVALID_CHARS
 *  13  is the last error preserved on success?
 *  14  cchWideChar longer than the string                    (is the count authoritative?)
 *  15  bulk equivalence with ntdll!RtlUnicodeToUTF8N over a fuzz corpus
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef LONG (NTAPI *F_ToUtf8N)(void*, ULONG, ULONG*, const wchar_t*, ULONG);
static F_ToUtf8N RtlU2U8;

#define SENTINEL 0xABCDEF01u

static int Q(const char* what, int cp, DWORD flags, const wchar_t* src, int cch,
             char* dst, int cb, const char* defc, BOOL* used)
{
    int r; DWORD e;
    SetLastError(SENTINEL);
    r = WideCharToMultiByte(cp, flags, src, cch, dst, cb, defc, used);
    e = GetLastError();
    printf("  %-56s -> ret %6d   lasterr %lu%s\n", what, r, (unsigned long)e,
           e == SENTINEL ? "  (UNTOUCHED)" : "");
    return r;
}

static void hex(const char* p, int n)
{
    int i; for (i = 0; i < n; ++i) printf("%02X ", (unsigned char)p[i]);
}

static unsigned long long rs = 0x243F6A8885A308D3ull;
static unsigned rnd(void){ rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

int main(void)
{
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    unsigned char* base = (unsigned char*)kb;
    wchar_t w[64], *big;
    char d[256], d2[256];
    BOOL used;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    RtlU2U8 = (F_ToUtf8N)GetProcAddress(nt, "RtlUnicodeToUTF8N");

    printf("== 1. the two imports the CP_UTF8 path calls ==\n");
    printf("  kernelbase.dll at %p, WideCharToMultiByte at %p (RVA %08X)\n",
           (void*)kb, (void*)GetProcAddress(kb, "WideCharToMultiByte"),
           (unsigned)((unsigned char*)GetProcAddress(kb, "WideCharToMultiByte") - base));
    {
        void* iat_conv = *(void**)(base + 0x29AD28);   /* called at 0x...54CDA */
        void* iat_err  = *(void**)(base + 0x29A860);   /* called through 0x...178A8 */
        printf("  IAT[0x29AD28] = %p   RtlUnicodeToUTF8N       = %p   %s\n",
               iat_conv, (void*)RtlU2U8, iat_conv == (void*)RtlU2U8 ? "SAME" : "different");
        printf("  IAT[0x29A860] = %p   RtlSetLastWin32Error    = %p   %s\n",
               iat_err, (void*)GetProcAddress(nt, "RtlSetLastWin32Error"),
               iat_err == (void*)GetProcAddress(nt, "RtlSetLastWin32Error") ? "SAME" : "different");
    }

    for (i = 0; i < 8; ++i) w[i] = (wchar_t)(L'a' + i);
    w[8] = 0;

    printf("\n== 2..6. the argument checks, in the order the shipped code asks them ==\n");
    Q("cchWideChar = 0", CP_UTF8, 0, w, 0, d, sizeof d, NULL, NULL);
    Q("cbMultiByte = -1", CP_UTF8, 0, w, 8, d, -1, NULL, NULL);
    Q("lpWideCharStr = NULL", CP_UTF8, 0, NULL, 8, d, sizeof d, NULL, NULL);
    Q("lpMultiByteStr = NULL, cbMultiByte = 16", CP_UTF8, 0, w, 8, NULL, 16, NULL, NULL);
    Q("lpMultiByteStr = NULL, cbMultiByte = 0  (measuring)", CP_UTF8, 0, w, 8, NULL, 0, NULL, NULL);
    Q("lpMultiByteStr == lpWideCharStr exactly", CP_UTF8, 0, w, 8, (char*)w, 16, NULL, NULL);
    Q("lpMultiByteStr = lpWideCharStr + 2 (OVERLAPPING)", CP_UTF8, 0, w, 4, ((char*)w) + 2, 8, NULL, NULL);
    Q("lpMultiByteStr == lpWideCharStr, cbMultiByte = 0", CP_UTF8, 0, w, 8, (char*)w, 0, NULL, NULL);
    /* restore w, the overlapping call above scribbled on it */
    for (i = 0; i < 8; ++i) w[i] = (wchar_t)(L'a' + i);
    w[8] = 0;

    printf("\n== 7. every dwFlags bit, one at a time (CP_UTF8) ==\n");
    {
        DWORD accepted = 0, rejected = 0;
        for (i = 0; i < 32; ++i) {
            DWORD f = 1u << i, e;
            int r;
            SetLastError(SENTINEL);
            r = WideCharToMultiByte(CP_UTF8, f, w, 8, d, sizeof d, NULL, NULL);
            e = GetLastError();
            if (r == 8) accepted |= f; else rejected |= f;
            if (r != 8 && e != 1004) printf("  bit %2d (%08lX): ret %d err %lu  <-- not ERROR_INVALID_FLAGS\n",
                                            i, (unsigned long)f, r, (unsigned long)e);
        }
        printf("  accepted mask = %08lX   rejected mask = %08lX\n",
               (unsigned long)accepted, (unsigned long)rejected);
        printf("  (0x0010 WC_DISCARDNS 0x0020 WC_SEPCHARS 0x0040 WC_DEFAULTCHAR\n"
               "   0x0080 WC_ERR_INVALID_CHARS 0x0200 WC_COMPOSITECHECK 0x0400 WC_NO_BEST_FIT_CHARS)\n");
    }

    printf("\n== 8. lpDefaultChar / lpUsedDefaultChar with CP_UTF8 (MSDN: both must be NULL) ==\n");
    Q("lpDefaultChar = \"?\"", CP_UTF8, 0, w, 8, d, sizeof d, "?", NULL);
    used = 0x5A;
    Q("lpUsedDefaultChar = &used, all-ASCII source", CP_UTF8, 0, w, 8, d, sizeof d, NULL, &used);
    printf("      used = %d\n", (int)used);
    {
        wchar_t lone[3]; lone[0] = L'a'; lone[1] = (wchar_t)0xD800; lone[2] = L'b';
        used = 0x5A;
        Q("lpUsedDefaultChar = &used, LONE SURROGATE source", CP_UTF8, 0, lone, 3, d, sizeof d, NULL, &used);
        printf("      used = %d\n", (int)used);
        used = 0x5A;
        Q("...and both non-NULL at once", CP_UTF8, 0, lone, 3, d, sizeof d, "?", &used);
        printf("      used = %d\n", (int)used);
    }
    Q("UTF-7 (65000) with lpDefaultChar", 65000, 0, w, 8, d, sizeof d, "?", NULL);
    Q("UTF-7 (65000) with lpUsedDefaultChar", 65000, 0, w, 8, d, sizeof d, NULL, &used);

    printf("\n== 9. negative cchWideChar ==\n");
    Q("cchWideChar = -1  (8 chars + NUL)", CP_UTF8, 0, w, -1, d, sizeof d, NULL, NULL);
    Q("cchWideChar = -2", CP_UTF8, 0, w, -2, d, sizeof d, NULL, NULL);
    Q("cchWideChar = -1000", CP_UTF8, 0, w, -1000, d, sizeof d, NULL, NULL);
    Q("cchWideChar = INT_MIN", CP_UTF8, 0, w, (int)0x80000000, d, sizeof d, NULL, NULL);
    { wchar_t e0[1]; e0[0] = 0;
      Q("cchWideChar = -1 on an EMPTY string", CP_UTF8, 0, e0, -1, d, sizeof d, NULL, NULL); }

    printf("\n== 10. the measuring mode ==\n");
    {
        memset(d, 0x5A, sizeof d);
        Q("cbMultiByte = 0, lpMultiByteStr = d (non-NULL)", CP_UTF8, 0, w, 8, d, 0, NULL, NULL);
        printf("      d[0..7] after = "); hex(d, 8); printf(" (0x5A means untouched)\n");
        Q("cbMultiByte = 0, lpMultiByteStr = NULL", CP_UTF8, 0, w, 8, NULL, 0, NULL, NULL);
        Q("cbMultiByte = 0, cchWideChar = -1", CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    }

    printf("\n== 11. a destination that is too small -- and WHAT IS LEFT IN IT ==\n");
    {
        wchar_t src[8]; ULONG produced = 0xDEAD; LONG st;
        for (i = 0; i < 8; ++i) src[i] = (wchar_t)(0x00E9);       /* 2 bytes each -> 16 */
        for (i = 0; i <= 17; ++i) {
            int r; DWORD e;
            memset(d, 0x5A, sizeof d);
            SetLastError(SENTINEL);
            r = WideCharToMultiByte(CP_UTF8, 0, src, 8, d, i, NULL, NULL);
            e = GetLastError();
            memset(d2, 0x5A, sizeof d2);
            st = RtlU2U8(i ? d2 : NULL, (ULONG)i, &produced, src, 16);
            printf("   cb=%2d  ret %3d  err %5lu | dst ", i, r, (unsigned long)e);
            hex(d, 18);
            printf("| Rtl st %08lX produced %lu  %s\n", (unsigned long)st, (unsigned long)produced,
                   memcmp(d, d2, 18) == 0 ? "SAME BYTES" : "DIFFER");
        }
    }

    printf("\n== 12. a lone surrogate, with and without WC_ERR_INVALID_CHARS ==\n");
    {
        wchar_t lone[4]; lone[0] = L'a'; lone[1] = (wchar_t)0xD800; lone[2] = L'b'; lone[3] = 0;
        memset(d, 0x5A, sizeof d);
        Q("flags = 0", CP_UTF8, 0, lone, 3, d, sizeof d, NULL, NULL);
        printf("      d = "); hex(d, 8); printf("\n");
        memset(d, 0x5A, sizeof d);
        Q("flags = WC_ERR_INVALID_CHARS", CP_UTF8, 0x80, lone, 3, d, sizeof d, NULL, NULL);
        printf("      d = "); hex(d, 8); printf("   <- was the buffer still written?\n");
        Q("flags = WC_ERR_INVALID_CHARS, measuring", CP_UTF8, 0x80, lone, 3, NULL, 0, NULL, NULL);
        Q("flags = WC_ERR_INVALID_CHARS, valid source", CP_UTF8, 0x80, w, 8, d, sizeof d, NULL, NULL);
        Q("flags = WC_ERR_INVALID_CHARS, too small", CP_UTF8, 0x80, lone, 3, d, 2, NULL, NULL);
    }

    printf("\n== 13. is the last error preserved on success? (SENTINEL = %08X) ==\n", SENTINEL);
    Q("an ordinary successful conversion", CP_UTF8, 0, w, 8, d, sizeof d, NULL, NULL);
    Q("a successful measuring call", CP_UTF8, 0, w, 8, NULL, 0, NULL, NULL);

    printf("\n== 14. is an explicit cchWideChar authoritative past the terminator? ==\n");
    {
        wchar_t t[8]; t[0]=L'a'; t[1]=0; t[2]=L'b'; t[3]=L'c';
        memset(d, 0x5A, sizeof d);
        i = Q("cch = 4 over \"a\\0bc\"", CP_UTF8, 0, t, 4, d, sizeof d, NULL, NULL);
        printf("      d = "); hex(d, (i > 0 ? i : 0)); printf("\n");
    }

    printf("\n== 15. bulk equivalence with RtlUnicodeToUTF8N (20000 random strings) ==\n");
    {
        long bad = 0, n;
        big = (wchar_t*)malloc(512 * 2);
        for (n = 0; n < 20000; ++n) {
            int len = (int)(rnd() % 120) + 1, cb, r; DWORD e; ULONG produced = 0; LONG st;
            int m = (int)(n & 3);
            for (i = 0; i < len; ++i) {
                unsigned x = rnd();
                big[i] = (m == 0) ? (wchar_t)(x % 128)
                       : (m == 1) ? (wchar_t)(x % 0x800)
                       : (m == 2) ? (wchar_t)(0xD000 + (x % 0x1200))
                                  : (wchar_t)x;
            }
            cb = (int)(rnd() % 300);
            memset(d, 0x5A, sizeof d);
            memset(d2, 0x5A, sizeof d2);
            SetLastError(SENTINEL);
            r = WideCharToMultiByte(CP_UTF8, 0, big, len, d, cb, NULL, NULL);
            e = GetLastError();
            st = RtlU2U8(cb ? d2 : NULL, (ULONG)cb, &produced, big, (ULONG)(len * 2));
            {
                int expect = (st < 0) ? 0 : (int)produced;
                DWORD expecte = (st == (LONG)0xC0000023) ? 122u
                              : (st < 0) ? 87u
                              : (produced == 0) ? 0u : SENTINEL;
                if (r != expect || e != expecte || memcmp(d, d2, sizeof d) != 0) {
                    if (++bad <= 8)
                        printf("  DISAGREE len=%d cb=%d: wc2mb %d/%lu   Rtl %08lX/%lu  bytes %s\n",
                               len, cb, r, (unsigned long)e, (unsigned long)st,
                               (unsigned long)produced,
                               memcmp(d, d2, sizeof d) ? "DIFFER" : "same");
                }
            }
        }
        printf("  disagreements: %ld / 20000\n", bad);
    }
    return 0;
}
