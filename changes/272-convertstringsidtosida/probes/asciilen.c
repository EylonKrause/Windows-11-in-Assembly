/* changes/272-convertstringsidtosida/probes/asciilen.c
 *
 * Two things the implementation cannot be written without.
 *
 * ------------------------------------------------------------------------------------------------
 * 1. Is ASCII invariant across the ANSI code pages?
 *
 * probes/codepage.c established that ConvertStringSidToSidA(s) is exactly
 * ConvertStringSidToSidW(MultiByteToWideChar(CP_ACP, 0, s, -1, ...)), zero disagreements over every
 * byte 0x01..0xFF in each of six field positions, every one of the 9025 printable ASCII pairs, and
 * the byte sequences that may not translate at all.
 *
 * So the ANSI form is a widening and then change 269's parser. The widening is the expensive part:
 * the shipped ANSI export costs 343.95 ns against 269.14 ns for the wide one, and MultiByteToWideChar
 * is not cheap.
 *
 * But a SID string is ASCII. If (and only if) every byte below 0x80 widens to the same code
 * point under every code page that can be an ACP, then the common case needs no code page at all:
 * it is a byte-to-word zero extension, which is one VPMOVZXBW per sixteen bytes. The fallback for
 * any input containing a byte at or above 0x80 is MultiByteToWideChar, called rather than imitated.
 *
 * "ASCII Is invariant" is exactly the kind of claim this project has been wrong about. It is
 * therefore MEASURED, over every byte 0x00..0x7F, against every code page Windows can use as an ACP
 *; the single-byte ones, the four DBCS ones, and UTF-8, which modern Windows can set as the ACP.
 * One counterexample and the fast path does not exist.
 *
 * ------------------------------------------------------------------------------------------------
 * 2. How long a string does it accept, and where does the widened copy live?
 *
 * The wide export takes up to 254 sub-authorities, so a legal SID string can be about 2800
 * characters, and an ILLEGAL one can be any length at all, because the caller chooses it. A
 * reimplementation that widened into a fixed stack buffer would either have to refuse strings the
 * shipped export accepts or overflow. So: what is the longest string the ANSI form accepts, is it
 * the same as the wide form's, and does a megabyte-long input refuse or crash?
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== 1. is every byte 0x00..0x7F the same code point under every ANSI code page? ==\n");
    {
        /* every code page Windows documents as usable for the system ANSI setting, plus UTF-8 */
        static const UINT CPS[] = {
            437, 850, 874, 932, 936, 949, 950, 1250, 1251, 1252, 1253, 1254, 1255, 1256,
            1257, 1258, 1361, 20127, 28591, 28592, 28605, 65001
        };
        unsigned i;
        int total_bad = 0, tested = 0;
        for (i = 0; i < sizeof CPS / sizeof CPS[0]; ++i) {
            CPINFO ci;
            int bad = 0, b;
            if (!IsValidCodePage(CPS[i]) || !GetCPInfo(CPS[i], &ci)) {
                printf("   cp %-6u not installed\n", CPS[i]);
                continue;
            }
            ++tested;
            for (b = 0; b <= 0x7F; ++b) {
                char in[2];
                wchar_t out[4];
                int n;
                in[0] = (char)b; in[1] = 0;
                n = MultiByteToWideChar(CPS[i], 0, in, 1, out, 4);
                if (n != 1 || out[0] != (wchar_t)b) {
                    if (bad < 4)
                        printf("   cp %-6u byte %02X -> %d unit(s)%s", CPS[i], b, n, n > 0 ? ", U+" : "");
                    if (bad < 4 && n > 0) printf("%04X", (unsigned)out[0]);
                    if (bad < 4) printf("   <<< NOT INVARIANT\n");
                    ++bad;
                }
            }
            printf("   cp %-6u (max char size %u): %d of 128 bytes are NOT invariant%s\n",
                   CPS[i], (unsigned)ci.MaxCharSize, bad, bad ? "" : "   -- ASCII is invariant");
            total_bad += bad;
        }
        printf("   %d code page(s) tested, %d counterexample(s) in total%s\n", tested, total_bad,
               total_bad ? "  <<< THE FAST PATH DOES NOT EXIST" : "");
    }

    printf("\n== 2. and is a byte AT OR ABOVE 0x80 ever the same as its own value? ==\n");
    printf("   (it does not have to be -- this is only to show the fast path's cut is meaningful)\n");
    {
        int b, same = 0;
        for (b = 0x80; b <= 0xFF; ++b) {
            char in[2]; wchar_t out[4];
            in[0] = (char)b; in[1] = 0;
            if (MultiByteToWideChar(CP_ACP, 0, in, 1, out, 4) == 1 && out[0] == (wchar_t)b) ++same;
        }
        printf("   under this machine's ACP (%u), %d of the 128 high bytes widen to their own value\n",
               (unsigned)GetACP(), same);
    }

    printf("\n== 3. how long a string does each form accept? ==\n");
    {
        static const int NS[] = { 1, 15, 100, 254, 255, 256, 1000, 10000, 100000 };
        unsigned i;
        char*   a = (char*)malloc(2000000);
        wchar_t* w = (wchar_t*)malloc(4000000);
        if (!a || !w) { printf("   out of memory\n"); return 1; }
        printf("   subs   bytes      ANSI                       wide\n");
        for (i = 0; i < sizeof NS / sizeof NS[0]; ++i) {
            int n = 0, k;
            PSID pa = 0, pw = 0;
            BOOL ra, rw;
            DWORD ea, ew;
            n += wsprintfA(a + n, "S-1-5");
            for (k = 0; k < NS[i]; ++k) n += wsprintfA(a + n, "-%d", (k % 9) + 1);
            MultiByteToWideChar(CP_ACP, 0, a, -1, w, 2000000);
            SetLastError(0); ra = ConvertStringSidToSidA(a, &pa); ea = GetLastError();
            SetLastError(0); rw = ConvertStringSidToSidW(w, &pw); ew = GetLastError();
            printf("   %6d %6d      %-3s err=%-8lu           %-3s err=%-8lu %s\n",
                   NS[i], n, ra ? "OK" : "NO", (unsigned long)ea, rw ? "OK" : "NO",
                   (unsigned long)ew,
                   ((ra ? 1 : 0) != (rw ? 1 : 0) || ea != ew) ? "  <<< THEY DIFFER" : "");
            if (pa) LocalFree(pa);
            if (pw) LocalFree(pw);
        }

        printf("\n   and a string that is long but NOT a SID at all:\n");
        for (i = 0; i < sizeof NS / sizeof NS[0]; ++i) {
            int len = NS[i] * 11 + 5, k;
            PSID pa = 0, pw = 0;
            BOOL ra, rw;
            DWORD ea, ew;
            if (len > 1999000) len = 1999000;
            for (k = 0; k < len; ++k) a[k] = (char)('a' + (k % 26));
            a[len] = 0;
            MultiByteToWideChar(CP_ACP, 0, a, -1, w, 2000000);
            SetLastError(0); ra = ConvertStringSidToSidA(a, &pa); ea = GetLastError();
            SetLastError(0); rw = ConvertStringSidToSidW(w, &pw); ew = GetLastError();
            printf("   %6d bytes of junk   %-3s err=%-8lu           %-3s err=%-8lu %s\n",
                   len, ra ? "OK" : "NO", (unsigned long)ea, rw ? "OK" : "NO", (unsigned long)ew,
                   ((ra ? 1 : 0) != (rw ? 1 : 0) || ea != ew) ? "  <<< THEY DIFFER" : "");
            if (pa) LocalFree(pa);
            if (pw) LocalFree(pw);
        }

        printf("\n   and a MEGABYTE of junk, which is where a fixed stack buffer would die:\n");
        {
            int k;
            PSID pa = 0;
            BOOL ra;
            for (k = 0; k < 1000000; ++k) a[k] = (char)('a' + (k % 26));
            a[1000000] = 0;
            SetLastError(0); ra = ConvertStringSidToSidA(a, &pa);
            printf("   1000000 bytes of junk  %-3s err=%lu\n", ra ? "OK" : "NO",
                   (unsigned long)GetLastError());
            if (pa) LocalFree(pa);
            /* and a megabyte that is a VALID prefix followed by junk */
            k = wsprintfA(a, "S-1-5-21");
            memset(a + k, 'z', 999000);
            a[k + 999000] = 0;
            SetLastError(0); ra = ConvertStringSidToSidA(a, &pa);
            printf("   a valid prefix + 999000 bytes of junk  %-3s err=%lu\n", ra ? "OK" : "NO",
                   (unsigned long)GetLastError());
            if (pa) LocalFree(pa);
        }
        free(a); free(w);
    }

    printf("\n== 4. what does the ANSI form cost against the wide one, on the same string? ==\n");
    {
        LARGE_INTEGER f, t0, t1;
        static const char*    A = "S-1-5-21-305419896-2596069104-287454020-1001";
        static const wchar_t* W = L"S-1-5-21-305419896-2596069104-287454020-1001";
        wchar_t wbuf[128];
        int i, pass;
        double best_a = 1e300, best_w = 1e300, best_m = 1e300;
        QueryPerformanceFrequency(&f);
        for (pass = 0; pass < 200; ++pass) {
            PSID p;
            QueryPerformanceCounter(&t0);
            for (i = 0; i < 2000; ++i) { p = 0; if (ConvertStringSidToSidA(A, &p)) LocalFree(p); }
            QueryPerformanceCounter(&t1);
            { double ns = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / 2000; if (ns < best_a) best_a = ns; }
            QueryPerformanceCounter(&t0);
            for (i = 0; i < 2000; ++i) { p = 0; if (ConvertStringSidToSidW(W, &p)) LocalFree(p); }
            QueryPerformanceCounter(&t1);
            { double ns = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / 2000; if (ns < best_w) best_w = ns; }
            QueryPerformanceCounter(&t0);
            for (i = 0; i < 2000; ++i) MultiByteToWideChar(CP_ACP, 0, A, -1, wbuf, 128);
            QueryPerformanceCounter(&t1);
            { double ns = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / 2000; if (ns < best_m) best_m = ns; }
        }
        printf("   ConvertStringSidToSidA          %8.2f ns\n", best_a);
        printf("   ConvertStringSidToSidW          %8.2f ns\n", best_w);
        printf("   MultiByteToWideChar alone       %8.2f ns   <- what the fast path would skip\n", best_m);
        printf("   the gap A - W                   %8.2f ns\n", best_a - best_w);
    }
    return 0;
}
