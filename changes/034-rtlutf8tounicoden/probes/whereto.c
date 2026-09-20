/* whereto.c -- WHERE do the missing stores land?
 *
 * badmap.c showed the variant writes the first 64 units correctly, never writes the rest, and
 * still returns the correct byte count. A correct count means the output cursor advanced, so the
 * stores were issued -- against some address other than the caller's buffer. That is memory
 * corruption, not a missing write, and this locates it.
 *
 * The destination is placed in the middle of a large region pre-filled with a sentinel. After the
 * call every touched 8-byte word in the whole region is reported, so a store below the buffer or
 * past its end shows up as an offset relative to dst.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern long wia_u82u(wchar_t*, unsigned long, unsigned long*, const char*, unsigned long);

#define PAD   (1u << 20)          /* a megabyte of sentinel on each side */
#define DSTB  (200u * 1024u)

int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 300;
    int k = argc > 2 ? atoi(argv[2]) : 0;

    size_t total = (size_t)PAD * 2 + DSTB;
    unsigned char* region = (unsigned char*)VirtualAlloc(NULL, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!region) { printf("alloc failed\n"); return 1; }
    memset(region, 0xAB, total);
    wchar_t* dst = (wchar_t*)(region + PAD);

    static unsigned char src[40000];
    for (int i = 0; i < n; ++i) src[i] = (unsigned char)('a' + (i & 15));
    if (k >= 0) src[k] = 0x80;

    ULONG produced = 0;
    long st = wia_u82u(dst, DSTB, &produced, (const char*)src, (ULONG)n);
    printf("n=%d bad@%d  status=%08lX  produced=%lu bytes (%lu units, expected %d)\n",
           n, k, (unsigned long)st, (unsigned long)produced,
           (unsigned long)produced / 2, n);

    /* report every touched run, as a byte offset relative to dst */
    printf("\ntouched runs, offset relative to dst (negative = BEFORE the buffer):\n");
    long long base = (long long)PAD;
    int runs = 0;
    size_t i = 0;
    while (i < total) {
        if (region[i] == 0xAB) { ++i; continue; }
        size_t s = i;
        while (i < total && region[i] != 0xAB) ++i;
        long long off = (long long)s - base;
        printf("  [%+lld .. %+lld]  %llu bytes%s\n", off, (long long)i - base,
               (unsigned long long)(i - s),
               (off < 0 || (unsigned long long)off >= DSTB) ? "   *** OUTSIDE THE CALLER'S BUFFER ***" : "");
        if (++runs > 20) { printf("  ... more\n"); break; }
    }
    if (!runs) printf("  none at all\n");
    return 0;
}
