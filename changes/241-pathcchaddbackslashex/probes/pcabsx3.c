/* changes/241-pathcchaddbackslashex/probes/pcabsx3.c
   THE PROTECTED PREFIX OF RemoveBackslashEx, measured -- and the cch ceilings, which differ per export.

   pcabsx2.c got AddBackslashEx almost exactly right and RemoveBackslashEx's root wrong, and turned up
   two ceiling facts that make the point of this whole exercise:

       cch = 2^31   AddBackslashEx S_OK        RemoveBackslashEx accepted
       cch = 2^32   AddBackslashEx E_INVALIDARG  RemoveBackslashEx accepted
       cch = 2^63   AddBackslashEx E_INVALIDARG  RemoveBackslashEx ACCEPTED

   So AddBackslashEx has a 32-bit ceiling and RemoveBackslashEx has NO ceiling at all -- while change
   240's PathCchRemoveFileSpec rejects anything above 0x8000. THREE FUNCTIONS IN ONE FAMILY, THREE
   DIFFERENT cch CEILINGS. Nothing about a shared rule in this family survives being assumed.

   THE ROOT IS A THIRD CONVENTION TOO. RemoveBackslashEx declines on "C:\", "\", "\\" and "\\?\C:\",
   and REMOVES from "\\srv\", "\\srv\shr\", "\\a\", "\\\" and "\\?\UNC\s\h\". That is neither change
   240's protected prefix (which EXCLUDES the root's trailing separator) nor PathCchSkipRoot's (which
   includes it and would protect "\\srv\" and "\\a\" too). The server and share are simply not
   protected here -- only the structural prefix is.

   So measure it. For every string, the fixed point of RemoveBackslashEx IS its protected prefix, by
   the same argument change 240 used: a function that refuses to cut into something lands exactly on
   it. This file tabulates that for every two-separator shape and fits the rule, then states the
   candidate and counts where it is wrong. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *PEX)(PWSTR, size_t, PWSTR*, size_t*);
static PEX addx, remx;
static wchar_t fp[512];

static int is_sep(wchar_t c){ return c == L'\\'; }

/* the fixed point of RemoveBackslashEx: apply until it stops returning S_OK */
static int fixed_point(const wchar_t* p, int n)
{
    memcpy(fp, p, (size_t)(n + 1) * 2);
    for (int g = 0; g < 600; ++g) {
        PWSTR e; size_t r;
        if (remx(fp, PATHCCH_MAX_CCH, &e, &r) != S_OK) break;
    }
    return (int)wcslen(fp);
}

/* THE CANDIDATE: the structural prefix only -- the server and share are not protected. */
static int is_drive_letter(wchar_t c)
{
    if (c >= L'A' && c <= L'Z') return 1;
    if (c >= L'a' && c <= L'z') return 1;
    if (c >= 0xC0 && c <= 0xD6) return 1;
    if (c >= 0xD8 && c <= 0xF6) return 1;
    if (c >= 0xF8 && c <= 0xFF) return 1;
    return 0;
}
static int protect(const wchar_t* p)
{
    if (!p[0]) return 0;
    if (is_sep(p[0])) {
        if (!is_sep(p[1])) return 1;                      /* "\" */
        if (p[2] == L'?' && is_sep(p[3])) {
            if ((p[4] == L'U' || p[4] == L'u') && (p[5] == L'N' || p[5] == L'n') &&
                (p[6] == L'C' || p[6] == L'c') && is_sep(p[7]))
                return 8;                                 /* "\\?\UNC\" */
            if (is_drive_letter(p[4]) && p[5] == L':')
                return is_sep(p[6]) ? 7 : 6;              /* "\\?\X:\" */
            return 1;
        }
        return 2;                                         /* "\\" -- the server is NOT protected */
    }
    if (is_drive_letter(p[0]) && p[1] == L':') return is_sep(p[2]) ? 3 : 2;
    return 0;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    addx = (PEX)GetProcAddress(hk, "PathCchAddBackslashEx");
    remx = (PEX)GetProcAddress(hk, "PathCchRemoveBackslashEx");
    if (!addx || !remx) { printf("cannot resolve\n"); return 1; }

    printf("=== 1. the AddBackslashEx ceiling, bisected ===\n");
    {
        size_t lo = (size_t)1 << 31, hi = (size_t)1 << 32;
        wchar_t a[32];
        PWSTR e; size_t r;
        wcscpy(a, L"C:\\dir");
        printf("  2^31 -> %08lX\n", (unsigned long)addx(a, lo, &e, &r));
        wcscpy(a, L"C:\\dir");
        printf("  2^32 -> %08lX\n", (unsigned long)addx(a, hi, &e, &r));
        while (lo + 1 < hi) {
            size_t mid = lo + (hi - lo) / 2;
            wcscpy(a, L"C:\\dir");
            if (addx(a, mid, &e, &r) == S_OK) lo = mid; else hi = mid;
        }
        printf("  the largest accepted cch is %#zx (%zu); the smallest rejected is %#zx\n",
               lo, lo, hi);
        printf("  => %s\n", (hi == ((size_t)1 << 32)) ? "the ceiling is 0xFFFFFFFF -- a 32-bit count"
                                                      : "the ceiling is somewhere unexpected");
    }

    printf("\n=== 2. does RemoveBackslashEx have ANY ceiling? ===\n");
    {
        wchar_t b[32];
        PWSTR e; size_t r;
        static const size_t V[] = { (size_t)1 << 32, (size_t)1 << 48, (size_t)1 << 62,
                                    (size_t)-1, (size_t)-2 };
        for (int i = 0; i < 5; ++i) {
            wcscpy(b, L"C:\\dir\\");
            printf("  cch=%#zx -> %08lX  \"%ls\"\n", V[i], (unsigned long)remx(b, V[i], &e, &r), b);
        }
    }

    printf("\n=== 3. the RemoveBackslashEx fixed point for every two-separator shape ===\n");
    printf("  %-16s %4s %9s %9s\n", "path", "n", "fixed", "candidate");
    {
        static const wchar_t* V[] = {
            L"\\", L"\\\\", L"\\\\\\", L"\\\\\\\\",
            L"\\a", L"\\a\\", L"\\\\a", L"\\\\a\\", L"\\\\a\\b", L"\\\\a\\b\\",
            L"\\\\a\\b\\c", L"\\\\a\\b\\c\\",
            L"C:", L"C:\\", L"C:\\a", L"C:\\a\\",
            L"\\\\?", L"\\\\?\\", L"\\\\?\\C:", L"\\\\?\\C:\\", L"\\\\?\\C:\\a\\",
            L"\\\\?\\UNC", L"\\\\?\\UNC\\", L"\\\\?\\UNC\\s", L"\\\\?\\UNC\\s\\",
            L"\\\\?\\UNC\\s\\h", L"\\\\?\\UNC\\s\\h\\", L"\\\\?\\a\\", L"\\\\?a\\",
            L"a", L"a\\", L"a\\\\", L"dir\\", L"", 0
        };
        long bad = 0;
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            int f = fixed_point(V[i], n);
            int c = protect(V[i]);
            /* the candidate predicts the fixed point only when the path is all-separators past the
               protected prefix; for the general case the fixed point is "strip trailing separators
               down to at least the protected prefix" */
            int expect = n;
            while (expect > c && is_sep(V[i][expect-1])) --expect;
            printf("  %-16ls %4d %9d %9d%s\n", V[i], n, f, expect,
                   f != expect ? "   <== DIFFERS" : "");
            if (f != expect) ++bad;
        }
        printf("\n    %ld of the shapes above disagree with the candidate\n", bad);
    }

    printf("\n=== 4. EXHAUSTIVE: the candidate protected prefix vs the measured fixed point ===\n");
    {
        static const wchar_t AL[5] = { L'a', L'\\', L':', L'?', L'C' };
        wchar_t s[16];
        long total = 0, bad = 0;
        int shown = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 5;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 5]; v /= 5; }
                s[len] = 0;
                int f = fixed_point(s, len);
                int pr = protect(s);
                int expect = len;
                while (expect > pr && is_sep(s[expect-1])) --expect;
                if (f != expect) {
                    ++bad;
                    if (shown < 20) {
                        printf("    \"%-8ls\" n=%d fixed=%d candidate=%d (protect=%d)\n",
                               s, len, f, expect, pr);
                        ++shown;
                    }
                }
                ++total;
            }
        }
        printf("\n    %ld strings, %ld disagreements with the candidate\n", total, bad);
        printf("    => %s\n", bad ? "the protected prefix rule is still wrong"
                                  : "the protected prefix is the STRUCTURAL prefix only -- the server\n"
                                    "       and share are not protected, unlike change 240's root");
    }

    printf("\n=== 5. and with U/N/C for the extended prefix ===\n");
    {
        static const wchar_t AL[7] = { L'a', L'\\', L':', L'?', L'U', L'N', L'C' };
        wchar_t s[16];
        long total = 0, bad = 0;
        int shown = 0;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 7;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 7]; v /= 7; }
                s[len] = 0;
                int f = fixed_point(s, len);
                int pr = protect(s);
                int expect = len;
                while (expect > pr && is_sep(s[expect-1])) --expect;
                if (f != expect) {
                    ++bad;
                    if (shown < 20) {
                        printf("    \"%-8ls\" n=%d fixed=%d candidate=%d (protect=%d)\n",
                               s, len, f, expect, pr);
                        ++shown;
                    }
                }
                ++total;
            }
        }
        printf("\n    %ld strings, %ld disagreements\n", total, bad);
    }
    return 0;
}
