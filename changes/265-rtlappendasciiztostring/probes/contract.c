/* changes/265-rtlappendasciiztostring/probes/contract.c
 *
 * What does ntdll!RtlAppendAsciizToString actually do?
 *
 *     NTSTATUS RtlAppendAsciizToString(PSTRING dest, PCSZ src)
 *
 * discovery/ntdll_rtl_uncovered2.c found it at **0.056 ns/byte**, four and a half times the per-byte
 * cost of its own siblings measured in the same run -- RtlCopyString and
 * RtlAppendUnicodeStringToString both sit at 0.012 -- which is the signature of a function that
 * makes a real `call` into strlen and then copies a byte at a time.
 *
 * Change 101 Already landed the wide analogue, RtlAppendUnicodeToString, and its contract is
 * written out in its header. None of it is assumed here. Changes 123 and 124 both found clear-side
 * routines carrying conventions their set-side twins did not, changes 214/215/216 found three
 * functions sharing one core and disagreeing about NULL and the empty set, and the SPACE bug got
 * into four landed changes at once by exactly this route. The narrow form counts BYTES where the
 * wide form counts characters, and the questions that matters for are asked below.
 *
 *   * src == NULL: success and no change, or an error?
 *   * the exact failure status, and whether dest is left untouched when it fires
 *   * is the boundary "fits in MaximumLength" or "fits WITH a terminator"?
 *   * is a NUL written, and under exactly what condition?
 *   * what happens at Length + strlen == MaximumLength, and one byte either side
 *   * a zero-length source, and a dest with no room at all
 *   * a source longer than a USHORT can describe
 *
 * Every case prints the whole destination buffer state -- status, Length, MaximumLength and the
 * bytes -- against a poison fill, because "unchanged on failure" is a claim about bytes that no
 * return value can make for you.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } ASTR;
typedef LONG (NTAPI *F_App)(ASTR*, const char*);

static F_App app;
static char buf[128];

static void ask(const char* what, const char* initial, USHORT maxlen, const char* src)
{
    ASTR d;
    LONG st;
    int i;
    memset(buf, '#', sizeof buf);                 /* poison, so any write at all is visible */
    d.Length = (USHORT)(initial ? strlen(initial) : 0);
    d.MaximumLength = maxlen;
    d.Buffer = buf;
    if (initial) memcpy(buf, initial, d.Length);

    st = app(&d, src);

    printf("  %-44s max=%2u src=%-9s -> %08lX  len=%2u  buf=[",
           what, maxlen, src ? (*src ? src : "\"\"") : "NULL", (unsigned long)st, d.Length);
    for (i = 0; i < (maxlen < 20 ? maxlen + 4 : 20); ++i)
        printf("%c", buf[i] ? (buf[i] == '#' ? '#' : buf[i]) : '.');
    printf("]\n");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    app = (F_App)GetProcAddress(h, "RtlAppendAsciizToString");
    if (!app) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== RtlAppendAsciizToString: the contract ==\n");
    printf("   '#' is untouched poison, '.' is a NUL that was written\n\n");

    printf("-- 1. the ordinary append --\n");
    ask("empty dest, room to spare", "", 16, "abc");
    ask("dest has 3, room to spare", "XYZ", 16, "abc");
    ask("a zero-length source", "XYZ", 16, "");
    ask("a NULL source", "XYZ", 16, NULL);

    printf("\n-- 2. THE BOUNDARY: does the terminator need room of its own? --\n");
    ask("3 + 3 into max 5 (does not fit)", "XYZ", 5, "abc");
    ask("3 + 3 into max 6 (fits EXACTLY, no room for NUL)", "XYZ", 6, "abc");
    ask("3 + 3 into max 7 (fits, one byte spare)", "XYZ", 7, "abc");
    ask("3 + 3 into max 8 (fits, two spare)", "XYZ", 8, "abc");

    printf("\n-- 3. a dest that is already full, and one with no room at all --\n");
    ask("dest already at max, appending nothing", "XYZ", 3, "");
    ask("dest already at max, appending 1", "XYZ", 3, "a");
    ask("max = 0, appending nothing", "", 0, "");
    ask("max = 0, appending 1", "", 0, "a");

    printf("\n-- 4. is the FAILURE really leaving the buffer alone? --\n");
    ask("1 byte too long", "XYZ", 5, "abc");
    ask("far too long", "XYZ", 6, "abcdefghijklmnop");

    printf("\n-- 5. a source longer than a USHORT can describe --\n");
    {
        static char big[70010];
        static char dest[70010];
        ASTR d;
        LONG st;
        int i;
        for (i = 0; i < 70000; ++i) big[i] = 'q';
        big[70000] = 0;
        memset(dest, '#', sizeof dest);
        d.Length = 0; d.MaximumLength = 65535; d.Buffer = dest;
        st = app(&d, big);
        printf("  %-44s max=%u src=70000 bytes -> %08lX  len=%u  first byte %c\n",
               "70000 bytes into a 65535-byte destination", 65535,
               (unsigned long)st, d.Length, dest[0]);

        for (i = 0; i < 70000; ++i) big[i] = 'q';
        big[65534] = 0;                               /* exactly 65534, which FITS in a USHORT */
        memset(dest, '#', sizeof dest);
        d.Length = 0; d.MaximumLength = 65535; d.Buffer = dest;
        st = app(&d, big);
        printf("  %-44s max=%u src=65534 bytes -> %08lX  len=%u  last byte %c, then %c\n",
               "65534 bytes into a 65535-byte destination", 65535,
               (unsigned long)st, d.Length, dest[65533], dest[65534] ? dest[65534] : '.');
    }

    printf("\n-- 6. THE SUM THAT WRAPS A USHORT --\n");
    printf("   Length + strlen can exceed 65535 while each fits in a USHORT on its own, and a\n");
    printf("   16-bit comparison would WRAP and conclude that it fits -- which is a buffer\n");
    printf("   overrun, not merely a wrong answer. 40000 + 30000 = 70000, and 70000 & 0xFFFF\n");
    printf("   is 4464, comfortably under any MaximumLength.\n");
    {
        static char big2[70010];
        static char dest2[70010];
        ASTR d;
        LONG st;
        int i;
        for (i = 0; i < 30000; ++i) big2[i] = 'r';
        big2[30000] = 0;
        memset(dest2, '#', sizeof dest2);
        d.Length = 40000; d.MaximumLength = 65535; d.Buffer = dest2;
        st = app(&d, big2);
        printf("  Length=40000 + 30000 bytes, max=65535 -> %08lX  len=%u  byte[40000]=%c  %s\n",
               (unsigned long)st, d.Length, dest2[40000],
               st ? "(refused, as a WIDE comparison must)" : "(ACCEPTED -- it wrote past the end)");
    }

    printf("\n-- 7. is Length ever updated when the status is a failure? --\n");
    {
        ASTR d;
        LONG st;
        memset(buf, '#', sizeof buf);
        memcpy(buf, "XYZ", 3);
        d.Length = 3; d.MaximumLength = 4; d.Buffer = buf;
        st = app(&d, "abcdef");
        printf("  status=%08lX, Length is %u (it was 3), MaximumLength is %u\n",
               (unsigned long)st, d.Length, d.MaximumLength);
    }
    return 0;
}
