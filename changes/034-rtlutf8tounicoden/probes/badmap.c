/* badmap.c -- the full written/unwritten map for one subject, and the smallest failing n.
 *
 * onebad.c narrowed it to: a single byte the blocks cannot decode (a continuation, C0/C1, or
 * F5..FF -- exactly the classes the dispatcher sends to the scalar window) makes every unit from
 * 64 + (k & ~7) onward go unwritten while the returned count stays correct. This prints the map
 * so the boundary is visible rather than inferred, and finds the smallest n that fails.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern long wia_u82u(wchar_t*, unsigned long, unsigned long*, const char*, unsigned long);
typedef long (NTAPI *FN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);
static FN sys;
static wchar_t a[40000], b[40000];
static unsigned char src[40000];

static void run(int n, int k, unsigned bv, ULONG* ra, ULONG* rb) {
    for (int i = 0; i < n; ++i) src[i] = (unsigned char)('a' + (i & 15));
    if (k >= 0) src[k] = (unsigned char)bv;
    memset(a, 0xAB, sizeof a); memset(b, 0xAB, sizeof b);
    *ra = *rb = 0;
    sys(a, sizeof a, ra, (const char*)src, (ULONG)n);
    wia_u82u(b, sizeof b, rb, (const char*)src, (ULONG)n);
}

int main(int argc, char** argv) {
    sys = (FN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlUTF8ToUnicodeN");
    int n = argc > 1 ? atoi(argv[1]) : 300;
    int k = argc > 2 ? atoi(argv[2]) : 0;
    ULONG ra, rb;

    printf("smallest failing n for one 0x80 at k=%d:\n", k);
    for (int t = k + 1; t <= 400; ++t) {
        run(t, k, 0x80, &ra, &rb);
        int bad = 0;
        for (ULONG i = 0; i < ra / 2 && i < rb / 2; ++i) if (a[i] != b[i]) { bad = (int)i + 1; break; }
        if (bad) { printf("  n=%d  first differing unit %d  (live %lu B, ours %lu B)\n",
                          t, bad - 1, (unsigned long)ra, (unsigned long)rb); break; }
    }

    run(n, k, 0x80, &ra, &rb);
    printf("\nmap for n=%d, 0x80 at k=%d   live=%lu B  ours=%lu B\n", n, k,
           (unsigned long)ra, (unsigned long)rb);
    printf("  '.' correct   'U' unwritten (0xABAB)   'X' written but wrong\n");
    ULONG lim = ra / 2;
    for (ULONG i = 0; i < lim; i += 64) {
        printf("  %5lu | ", (unsigned long)i);
        for (ULONG j = i; j < i + 64 && j < lim; ++j)
            putchar(a[j] == b[j] ? '.' : (b[j] == 0xABAB ? 'U' : 'X'));
        putchar('\n');
    }
    return 0;
}
