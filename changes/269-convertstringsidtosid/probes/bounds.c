/* changes/269-convertstringsidtosid/probes/bounds.c
 *
 * The last two things the implementation needs to know.
 *
 *   1. How many sub-authorities does the parser accept? probes/limits.c found it happily building
 *      a twenty-sub-authority SID that the formatter then refused, so the parser is not bounded by
 *      the documented fifteen. The allocation size is 8 + 4*count, so where the count stops is
 *      where the allocation stops, and guessing it wrong is a heap question rather than a
 *      formatting one.
 *
 *   2. What happens to the output pointer on failure. a caller that checks the return value and
 *      then frees unconditionally behaves differently depending on the answer, and a
 *      reimplementation that cleared the pointer where the original does not, or the reverse --
 *      would differ in a way no status comparison would catch.
 *
 * Both are read off the live export.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    static wchar_t buf[40000];
    int lo = 1, hi = 6000, k;
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== how many sub-authorities does the PARSER accept? ==\n");
    for (k = 1; k <= 6000; ) {
        PSID sid = 0;
        int i, n = 0;
        n += wsprintfW(buf + n, L"S-1-5");
        for (i = 0; i < k; ++i) n += wsprintfW(buf + n, L"-%d", (i % 9) + 1);
        if (ConvertStringSidToSidW(buf, &sid) && sid) {
            unsigned char* b = (unsigned char*)sid;
            if (k <= 3 || k == 15 || k == 16 || k == 255 || k == 256 || k == 257 || k >= 4000)
                printf("  %5d sub-authorities: OK   count byte = %3u, GetLengthSid = %lu\n",
                       k, b[1], (unsigned long)GetLengthSid(sid));
            LocalFree(sid);
            lo = k;
        } else {
            printf("  %5d sub-authorities: REFUSED (err %lu)  <== the first refusal\n",
                   k, (unsigned long)GetLastError());
            hi = k;
            break;
        }
        k = (k < 16) ? k + 1 : (k < 300 ? k + 1 : k + 500);
    }
    printf("  accepted up to %d; first refusal at %d\n", lo, hi);

    printf("\n== the count byte is ONE byte: what does it hold past 255? ==\n");
    for (k = 254; k <= 258; ++k) {
        PSID sid = 0;
        int i, n = 0;
        n += wsprintfW(buf + n, L"S-1-5");
        for (i = 0; i < k; ++i) n += wsprintfW(buf + n, L"-%d", (i % 9) + 1);
        if (ConvertStringSidToSidW(buf, &sid) && sid) {
            unsigned char* b = (unsigned char*)sid;
            printf("  %3d -> count byte %3u, GetLengthSid %lu %s\n", k, b[1],
                   (unsigned long)GetLengthSid(sid),
                   b[1] == (unsigned char)k ? "" : "  <== the byte does not match the input");
            LocalFree(sid);
        } else {
            printf("  %3d -> REFUSED (err %lu)\n", k, (unsigned long)GetLastError());
        }
    }

    printf("\n== the output pointer on FAILURE ==\n");
    {
        static const wchar_t* BAD[5] = { L"", L"S", L"S-1-5", L"ZZ", L"S-1-5-x" };
        int i;
        for (i = 0; i < 5; ++i) {
            PSID sid = (PSID)(UINT_PTR)0xDEADBEEF;
            BOOL ok = ConvertStringSidToSidW(BAD[i], &sid);
            printf("  %-12ls -> %s, the pointer is %s\n", BAD[i], ok ? "OK" : "FALSE",
                   sid == (PSID)(UINT_PTR)0xDEADBEEF ? "LEFT ALONE"
                                                     : (sid == 0 ? "cleared to NULL" : "something else"));
        }
    }

    printf("\n== and a NULL argument each way ==\n");
    {
        PSID sid = 0;
        SetLastError(0);
        printf("  ConvertStringSidToSidW(NULL, &sid) -> %s, err %lu\n",
               ConvertStringSidToSidW(0, &sid) ? "OK" : "FALSE", (unsigned long)GetLastError());
        SetLastError(0);
        printf("  ConvertStringSidToSidW(L\"S-1-5-18\", NULL) -> %s, err %lu\n",
               ConvertStringSidToSidW(L"S-1-5-18", 0) ? "OK" : "FALSE", (unsigned long)GetLastError());
    }
    return 0;
}
