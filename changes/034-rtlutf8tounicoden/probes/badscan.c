/* badscan.c: where exactly does the TGL variant stop writing on the bad32 class?
 *
 * classcheck.c showed the variant returns the CORRECT status and the CORRECT byte count on
 * bad32 at n>=512 while leaving the destination at wchar 88 onward unwritten. A correct count
 * with a short write means the output cursor kept advancing while the stores stopped, so the
 * question is which block stops storing and at what source offset.
 *
 * This sweeps n and reports, for each, the first wchar that differs from the live export and
 * whether ours wrote it at all. The SHAPE of stop-index against n localizes the block.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern long wia_u82u(wchar_t*, unsigned long, unsigned long*, const char*, unsigned long);
typedef long (NTAPI *FN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);

static void fill_bad(unsigned char* s, int n, int period) {
    for (int i = 0; i < n; ++i) s[i] = (unsigned char)('a' + (i & 15));
    for (int i = period - 1; i < n; i += period) s[i] = 0x80;
}

int main(int argc, char** argv) {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    FN sys = (FN)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    if (!sys) return 2;

    static unsigned char src[40000];
    static wchar_t a[40000], b[40000];
    int period = argc > 1 ? atoi(argv[1]) : 32;

    printf("bad byte every %d bytes.  'stop' = first wchar ours did not write.\n", period);
    printf("%-7s %-9s %-9s %-7s %-7s %s\n", "n", "live by", "ours by", "stop", "n-stop", "note");
    printf("----------------------------------------------------------------\n");
    for (int n = 64; n <= 1024; n += (n < 200 ? 1 : 64)) {
        fill_bad(src, n, period);
        ULONG ra = 0, rb = 0;
        memset(a, 0xAB, sizeof a); memset(b, 0xAB, sizeof b);
        long sa = sys(a, sizeof a, &ra, (const char*)src, (ULONG)n);
        long sb = wia_u82u(b, sizeof b, &rb, (const char*)src, (ULONG)n);
        int stop = -1, unwritten = 0;
        ULONG lim = (ra < rb ? ra : rb) / 2;
        for (ULONG i = 0; i < lim; ++i)
            if (a[i] != b[i]) { stop = (int)i; unwritten = (b[i] == 0xABAB); break; }
        if (stop < 0 && sa == sb && ra == rb) {
            if (n < 200 && (n % 16)) continue;                 /* keep the clean rows sparse */
            printf("%-7d %-9lu %-9lu %-7s %-7s ok\n", n, (unsigned long)ra, (unsigned long)rb, "-", "-");
        } else {
            printf("%-7d %-9lu %-9lu %-7d %-7d %s%s\n", n, (unsigned long)ra, (unsigned long)rb,
                   stop, stop < 0 ? 0 : n - stop,
                   unwritten ? "NOT WRITTEN" : "wrong value",
                   sa != sb ? " +status differs" : "");
        }
    }
    return 0;
}
