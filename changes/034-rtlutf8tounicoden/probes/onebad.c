/* onebad.c -- one malformed byte at offset k in an otherwise-ASCII buffer.
 *
 * badscan.c showed the stop index depends on the spacing of the malformed bytes (16->72,
 * 32->88, 33->96) and that pure ASCII is clean. A single bad byte at a swept offset isolates
 * which block mishandles it, and a second sweep over the BYTE VALUE says whether it is the
 * class of the malformed byte or its position that matters.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern long wia_u82u(wchar_t*, unsigned long, unsigned long*, const char*, unsigned long);
typedef long (NTAPI *FN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);
static FN sys;

/* returns: 0 clean, else 1 + first differing wchar; *unw set if ours never wrote it */
static int check(const unsigned char* src, int n, int* unw, int* cnt_ok) {
    static wchar_t a[40000], b[40000];
    ULONG ra = 0, rb = 0;
    memset(a, 0xAB, sizeof a); memset(b, 0xAB, sizeof b);
    long sa = sys(a, sizeof a, &ra, (const char*)src, (ULONG)n);
    long sb = wia_u82u(b, sizeof b, &rb, (const char*)src, (ULONG)n);
    *cnt_ok = (sa == sb && ra == rb);
    ULONG lim = (ra < rb ? ra : rb) / 2;
    for (ULONG i = 0; i < lim; ++i)
        if (a[i] != b[i]) { *unw = (b[i] == 0xABAB); return (int)i + 1; }
    return *cnt_ok ? 0 : -1;
}

int main(int argc, char** argv) {
    sys = (FN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlUTF8ToUnicodeN");
    if (!sys) return 2;
    static unsigned char src[40000];
    int n = argc > 1 ? atoi(argv[1]) : 300;
    unsigned bv = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0) : 0x80;

    printf("n=%d, one bad byte 0x%02X at offset k\n", n, bv);
    int firstbad = -1, nbad = 0;
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) src[i] = (unsigned char)('a' + (i & 15));
        src[k] = (unsigned char)bv;
        int unw = 0, cnt_ok = 0;
        int r = check(src, n, &unw, &cnt_ok);
        if (r != 0) {
            ++nbad;
            if (firstbad < 0) firstbad = k;
            if (nbad <= 12)
                printf("  k=%-5d stop=%-6d %s%s\n", k, r - 1,
                       unw ? "NOT WRITTEN" : "wrong value", cnt_ok ? "" : "  +count/status differs");
        }
    }
    printf("  %d of %d offsets fail; first at k=%d\n", nbad, n, firstbad);

    if (nbad) {
        printf("\nsame k, sweeping the malformed byte VALUE (k=%d):\n", firstbad);
        static const unsigned vals[] = { 0x80, 0x9F, 0xBF, 0xC0, 0xC1, 0xF5, 0xFF, 0xE0, 0xF0 };
        for (int v = 0; v < 9; ++v) {
            for (int i = 0; i < n; ++i) src[i] = (unsigned char)('a' + (i & 15));
            src[firstbad] = (unsigned char)vals[v];
            int unw = 0, cnt_ok = 0;
            int r = check(src, n, &unw, &cnt_ok);
            printf("  0x%02X -> %s\n", vals[v],
                   r == 0 ? "clean" : (unw ? "NOT WRITTEN" : "wrong value"));
        }
    }
    return 0;
}
