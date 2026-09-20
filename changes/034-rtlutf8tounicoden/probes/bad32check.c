/* bad32check.c -- independent check of the bench's `bad32` class against the live export.
 *
 * Why. changes/034's tgl variant reports bad32 at ~9.4 ns for 64 bytes and for 32000 bytes --
 * constant time regardless of length, which would be 3379 GB/s. ntdll takes 12000 ns for the same
 * 32000 bytes, so ntdll is processing them. correctness.c passes 327,758 cases, so either the
 * variant is genuinely returning the right answer without looking at the input (impossible), or
 * the corpus cannot express this particular input shape.
 *
 * This asks the live export and the variant the same question directly, on exactly the bench's
 * subject, and compares the STATUS, the returned byte count and every output byte.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern long wia_u82u(wchar_t*, unsigned long, unsigned long*, const char*, unsigned long);
typedef long (NTAPI *FN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);

/* the bench's bad32: ASCII with one malformed byte every 32 */
static void fill_bad32(unsigned char* s, int n) {
    for (int i = 0; i < n; ++i) s[i] = (unsigned char)('a' + (i & 15));
    for (int i = 31; i < n; i += 32) s[i] = 0x80;      /* a lone continuation byte */
}

int main(void) {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    FN sys = (FN)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    if (!sys) { printf("cannot resolve RtlUTF8ToUnicodeN\n"); return 2; }

    static unsigned char src[40000];
    static wchar_t a[40000], b[40000];
    static const int LENS[] = { 64, 512, 4000, 32000 };

    int bad = 0;
    for (int k = 0; k < 4; ++k) {
        int n = LENS[k];
        fill_bad32(src, n);
        ULONG ra = 0xCDCDCDCD, rb = 0xCDCDCDCD;
        memset(a, 0xAB, sizeof a);
        memset(b, 0xAB, sizeof b);

        long sa = sys(a, sizeof a, &ra, (const char*)src, (ULONG)n);
        long sb = wia_u82u(b, sizeof b, &rb, (const char*)src, (ULONG)n);

        int same_bytes = (ra == rb) && (ra == 0xCDCDCDCD || memcmp(a, b, ra) == 0);
        printf("n=%-6d  live: status=%08lX bytes=%-7lu | ours: status=%08lX bytes=%-7lu | %s\n",
               n, (unsigned long)sa, (unsigned long)ra,
                  (unsigned long)sb, (unsigned long)rb,
               (sa == sb && same_bytes) ? "MATCH" : "*** MISMATCH ***");
        if (!(sa == sb && same_bytes)) {
            ++bad;
            if (ra != rb) printf("        byte counts differ: live %lu vs ours %lu\n",
                                 (unsigned long)ra, (unsigned long)rb);
            else {
                ULONG lim = ra / 2;
                for (ULONG i = 0; i < lim; ++i)
                    if (a[i] != b[i]) { printf("        first differing wchar at %lu: live U+%04X ours U+%04X\n",
                                               (unsigned long)i, a[i], b[i]); break; }
            }
        }
    }
    printf("\n%s\n", bad ? "THE VARIANT DOES NOT MATCH THE LIVE EXPORT ON THIS CLASS"
                         : "all four lengths match the live export");
    return bad ? 1 : 0;
}
