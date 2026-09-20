/* changes/129-rtlchartointeger/probes/pastnul2.c
 *
 * Pinning the rule probes/pastnul.c found.
 *
 * pastnul.c established that the shipped export reads past a terminator, but only just:
 *
 *      00 36                 -> 6          one leading NUL, then a digit: PARSED
 *      00 31 32 33 34        -> 1234       likewise
 *      00 61 62 63  base 16  -> 0xABC      likewise
 *      00 00 39 39           -> 0          two leading NULs: STOPPED
 *      20 00 38              -> 0          a space then a NUL: STOPPED
 *      2D 00 39              -> 0          a sign then a NUL: STOPPED (no digits)
 *      eight NULs at a page edge           did NOT fault
 *
 * So exactly one NUL is stepped over, and only at the very start. That is a strange enough shape that
 * guessing the code behind it is a bad idea; every guess so far has been contradicted by one of the
 * rows above. A `while (*s && (signed char)*s <= ' ')` loop stops at the first NUL and cannot produce 6.
 * A loop that consumes the NUL and then continues would produce 8 for `20 00 38`, and it does not. A
 * `do/while` that reads before testing loses the first character of "42", which the bench shows it does
 * not.
 *
 * This probe therefore maps the rule by measurement across every arrangement that matters: a NUL at each
 * position among the other kinds of skipped byte, before and after a sign, and with the digit at varying
 * distance. The output is a TRUTH TABLE, printed row by row rather than summarised, because the whole
 * lesson of change 239 is that a summary hides the contradictions that identify the real rule.
 *
 * build:  cl /nologo /O2 pastnul2.c /Fe:pastnul2.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_C2I)(const char*, ULONG, ULONG*);
static F_C2I live;
extern long wia_char2int(const char*, unsigned long, unsigned long*);

static void row(const char* label, const unsigned char* bytes, int n, ULONG base)
{
    static char buf[64];
    ULONG v1 = 0xA5A5A5A5ul, v2 = 0xA5A5A5A5ul;
    NTSTATUS s1, s2;
    int q;
    memset(buf, 0, sizeof buf);
    for (q = 0; q < n; ++q) buf[q] = (char)bytes[q];
    s1 = live(buf, base, &v1);
    s2 = (NTSTATUS)wia_char2int(buf, base, (unsigned long*)&v2);
    printf("   %-30s", label);
    for (q = 0; q < n; ++q) printf(" %02X", bytes[q]);
    for (; q < 7; ++q) printf("   ");
    printf("  base %-3lu live %08lX/%08lX  ours %08lX/%08lX  %s\n",
           base, (unsigned long)s1, (unsigned long)v1, (unsigned long)s2, (unsigned long)v2,
           (s1 == s2 && v1 == v2) ? "agree" : "*** DIFFER ***");
}

int main(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    live = (F_C2I)GetProcAddress(nt, "RtlCharToInteger");
    if (!live) { printf("not exported\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== the truth table for a NUL inside the leading run ==\n");
    printf("   (every row plants the bytes shown in a zeroed 64-byte buffer and asks both)\n\n");

    printf("-- a NUL at position 0, then a digit at varying distance\n");
    { static const unsigned char b[] = { 0x00, '6' };                      row("NUL '6'", b, 2, 10); }
    { static const unsigned char b[] = { 0x00, '4', '2' };                 row("NUL '4' '2'", b, 3, 10); }
    { static const unsigned char b[] = { 0x00, 0x20, '6' };                row("NUL sp '6'", b, 3, 10); }
    { static const unsigned char b[] = { 0x00, 0x09, '6' };                row("NUL tab '6'", b, 3, 10); }
    { static const unsigned char b[] = { 0x00, 0x80, '6' };                row("NUL 80 '6'", b, 3, 10); }
    { static const unsigned char b[] = { 0x00, 0x00, '6' };                row("NUL NUL '6'", b, 3, 10); }
    { static const unsigned char b[] = { 0x00, '-', '6' };                 row("NUL '-' '6'", b, 3, 10); }
    { static const unsigned char b[] = { 0x00, '+', '6' };                 row("NUL '+' '6'", b, 3, 10); }

    printf("\n-- a NUL later in the run\n");
    { static const unsigned char b[] = { 0x20, 0x00, '8' };                row("sp NUL '8'", b, 3, 10); }
    { static const unsigned char b[] = { 0x09, 0x00, '8' };                row("tab NUL '8'", b, 3, 10); }
    { static const unsigned char b[] = { 0x80, 0x00, '8' };                row("80 NUL '8'", b, 3, 10); }
    { static const unsigned char b[] = { 0x20, 0x20, 0x00, '8' };          row("sp sp NUL '8'", b, 4, 10); }
    { static const unsigned char b[] = { 0x20, 0x00, 0x00, '8' };          row("sp NUL NUL '8'", b, 4, 10); }

    printf("\n-- a NUL around the sign\n");
    { static const unsigned char b[] = { '-', 0x00, '9' };                 row("'-' NUL '9'", b, 3, 10); }
    { static const unsigned char b[] = { 0x20, '-', 0x00, '9' };           row("sp '-' NUL '9'", b, 4, 10); }
    { static const unsigned char b[] = { 0x00, '-', '9' };                 row("NUL '-' '9'", b, 3, 10); }

    printf("\n-- and a NUL inside the digits, which must simply end the number\n");
    { static const unsigned char b[] = { '1', 0x00, '2' };                 row("'1' NUL '2'", b, 3, 10); }
    { static const unsigned char b[] = { 0x00, '1', 0x00, '2' };           row("NUL '1' NUL '2'", b, 4, 10); }

    printf("\n-- the base-0 prefix forms behind a leading NUL\n");
    { static const unsigned char b[] = { 0x00, '0', 'x', '1', 'f' };       row("NUL \"0x1f\"", b, 5, 0); }
    { static const unsigned char b[] = { 0x00, '0', '7', '7', '7' };       row("NUL \"0777\"", b, 5, 0); }

    printf("\n-- an all-NUL string, which is the case a caller is most likely to pass by accident\n");
    { static const unsigned char b[] = { 0x00 };                           row("NUL only", b, 1, 10); }
    { static const unsigned char b[] = { 0x00, 0x00 };                     row("NUL NUL", b, 2, 10); }
    return 0;
}
