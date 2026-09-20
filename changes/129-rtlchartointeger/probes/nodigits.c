/* changes/129-rtlchartointeger/probes/nodigits.c
 *
 * Why this probe exists: the live-substitution gate refused to pass, and it was right.
 *
 * RESULTS.md has recorded, since the change was first written, that
 *
 *      "No digits is still STATUS_SUCCESS with value 0 (\"\", \"abc\")."
 *
 * The correctness gate agreed, over three-way comparison against the live export, for every base. The
 * live-substitution harness then disagreed 291 times out of 40000, every single one of them a string
 * with NO DIGITS, with the live export returning STATUS_SUCCESS and a value of 6, 7, 3, 5, 0x000000BA,
 * 0x08225F8F, 0xB26A432B ... and ours returning 0.
 *
 * Those look like values left behind by earlier calls, which would mean the export writes an
 * UNINITIALISED local when it has no digits to report, and that the recorded contract is wrong.
 *
 * And it would explain why the correctness gate could not see it. That gate compares three ULONGs after
 * three calls; if its own were zero-initialised, then "the export left the caller's word alone" and "the
 * export wrote 0" are the same observation. The sentinel was the value under test, the vacuous-corpus
 * defect this project keeps meeting, in its purest form: a filler indistinguishable from a result.
 *
 * So this probe asks the question three ways that cannot be confused:
 *
 *   1. call with a no-digit string having first put a KNOWN, DISTINCTIVE value in the destination, and
 *      report what comes back. If the known value survives, the export did not write. If a previous
 *      parse's value comes back, it wrote something stale;
 *   2. interleave: parse a known number, then parse a no-digit string into a FRESH destination, and see
 *      whether the second call reports the first call's number;
 *   3. sweep the no-digit strings, "", "abc", "z", " ", "+", "-", "0x" with base 16 forced, a lone
 *      high byte, against several bases, repeating each with two different sentinels, and report
 *      whether the answer depends on the sentinel, on the previous call, or on nothing.
 *
 * build:  cl /nologo /O2 nodigits.c /Fe:nodigits.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_C2I)(const char*, ULONG, ULONG*);
static F_C2I live;

int main(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    int i, k;

    live = (F_C2I)GetProcAddress(nt, "RtlCharToInteger");
    if (!live) { printf("not exported\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== RtlCharToInteger with NO DIGITS: what does it write? ==\n");

    /* --- 1. a distinctive sentinel, on its own ------------------------------------------------- */
    printf("\n-- 1. one call, with a distinctive sentinel already in the destination\n");
    {
        static const unsigned long sents[] = { 0x00000000ul, 0xA5A5A5A5ul, 0xFFFFFFFFul, 0xDEADBEEFul };
        for (k = 0; k < 4; ++k) {
            ULONG v = sents[k];
            NTSTATUS st = live("", 10, &v);
            printf("   sentinel %08lX -> status %08lX, value %08lX  %s\n",
                   sents[k], (unsigned long)st, (unsigned long)v,
                   v == sents[k] ? "(UNTOUCHED)" : "(WRITTEN)");
        }
        printf("   if the value follows the sentinel, the export never writes it, and a gate whose\n");
        printf("   sentinel is 0 cannot tell that from a written 0.\n");
    }

    /* --- 2. interleaved with a real parse ------------------------------------------------------ */
    printf("\n-- 2. parse a known number, then a no-digit string into a FRESH destination\n");
    {
        static const unsigned long probes[] = { 1234567u, 0xABCDEFu, 42u, 999999999u };
        for (k = 0; k < 4; ++k) {
            char buf[24];
            ULONG a = 0x11111111ul, b = 0x22222222ul;
            NTSTATUS s1, s2;
            sprintf(buf, "%lu", probes[k]);
            s1 = live(buf, 10, &a);
            s2 = live("", 10, &b);
            printf("   parsed %-10lu -> %08lX (st %08lX);  then \"\" -> %08lX (st %08lX)  %s\n",
                   probes[k], (unsigned long)a, (unsigned long)s1,
                   (unsigned long)b, (unsigned long)s2,
                   b == a ? "*** THE PREVIOUS CALL'S VALUE ***"
                          : (b == 0x22222222ul ? "(its own sentinel: untouched)" : "(something else)"));
        }
    }

    /* --- 3. the sweep ------------------------------------------------------------------------- */
    printf("\n-- 3. every no-digit shape, two sentinels each, across four bases. A row where the two\n");
    printf("      sentinels come back unchanged is an export that does not write; a row where both\n");
    printf("      come back as the SAME foreign number is an export writing something stale.\n");
    {
        static const char* strs[] = { "", "abc", "z", " ", "\t", "+", "-", "- 42", "0x", "\x80", "/" };
        static const unsigned long bases[] = { 0, 8, 10, 16 };
        int untouched = 0, written = 0, total = 0;
        for (i = 0; i < (int)(sizeof(strs) / sizeof(strs[0])); ++i) {
            for (k = 0; k < 4; ++k) {
                ULONG v1 = 0x5A5A5A5Aul, v2 = 0xC3C3C3C3ul;
                NTSTATUS s1 = live(strs[i], bases[k], &v1);
                NTSTATUS s2 = live(strs[i], bases[k], &v2);
                int u = (v1 == 0x5A5A5A5Aul && v2 == 0xC3C3C3C3ul);
                ++total;
                if (u) ++untouched; else ++written;
                if (k == 0 || !u) {
                    int q;
                    printf("      bytes:");
                    for (q = 0; strs[i][q]; ++q) printf(" %02X", (unsigned char)strs[i][q]);
                    if (!strs[i][0]) printf(" (empty)");
                    printf("  base %-10lu st %08lX  v1 %08lX  v2 %08lX  %s\n",
                           bases[k], (unsigned long)s1, (unsigned long)v1, (unsigned long)v2,
                           u ? "UNTOUCHED" : (v1 == v2 ? "WROTE THE SAME FOREIGN VALUE" : "WROTE, AND DIFFERENTLY"));
                }
                (void)s2;
            }
        }
        printf("\n      %d combinations: %d left BOTH sentinels alone, %d wrote something\n",
               total, untouched, written);
        printf("      %s\n", written == 0
               ? "=> with no digits the export DOES NOT WRITE the caller's ULONG at all"
               : "=> with no digits the export writes something; see the rows above for what");
    }

    /* --- 4. and the one that matters for the contract: is a WRITTEN no-digit value repeatable?, */
    printf("\n-- 4. is whatever it writes REPRODUCIBLE? The same call, same sentinel, twenty times.\n");
    {
        ULONG first = 0;
        int same = 1;
        for (k = 0; k < 20; ++k) {
            ULONG v = 0x77777777ul;
            live("abc", 10, &v);
            if (k == 0) first = v;
            else if (v != first) same = 0;
        }
        printf("      first %08lX, all twenty %s\n", (unsigned long)first,
               same ? "identical" : "NOT identical");
        printf("      %s\n", same
               ? "so within one call pattern it is stable, which is how a gate can miss it"
               : "so it is not even stable call to call");
    }
    return 0;
}
