/* changes/268-rtlunicodestringtoutf8string/probes/failwrite.c
 *
 * WHAT DOES A FAILING CALL LEAVE IN THE DESTINATION BUFFER?
 *
 * probes/statuses.c established the statuses and the terminator rule. It did NOT establish what
 * happens to the BYTES on a failing call, and the first draft of this change assumed the two
 * directions behave the same way there, because they are documented as mirror images. The
 * correctness gate disagreed: status, Length and MaximumLength all matched live on every one of
 * 84434 cases, and 32784 of them still failed -- the only field left is the buffer.
 *
 * So this file asks the question directly. A destination is poisoned with a recognisable fill, the
 * live export is called with a capacity too small for the result, and every byte that is no longer
 * the poison is printed. Both directions, every capacity from 0 up to past the required size.
 *
 * The answer decides how the wrapper must be built, and the two answers are not the same:
 *
 *   UTF-16 -> UTF-8 : a failing call PARTIALLY FILLS the buffer with as much as fit. That is the
 *                     N-form's own behaviour showing through, so the wrapper can hand the caller's
 *                     buffer straight to the N-form and let it write what it can -- one pass.
 *   UTF-8 -> UTF-16 : a failing call writes NOTHING. The destination comes back untouched, which
 *                     means the shipped code decides it will not fit BEFORE it converts anything --
 *                     it sizes first. A wrapper that converts straight into the caller's buffer
 *                     cannot reproduce that, because by the time the N-form reports the shortfall
 *                     it has already written the part that fit.
 *
 * That asymmetry is the whole reason the second direction is built differently from the first.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG (NTAPI *F_U2U8)(U8STR*, const USTR*, BOOLEAN);
typedef LONG (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);

static F_U2U8 l_u2u8;
static F_U82U l_u82u;

#define CAP 64
static char    b8[CAP];
static wchar_t bw[CAP];

static void show8(const wchar_t* s, int wlen, int maxlen)
{
    USTR in;
    U8STR d;
    LONG r;
    int k, last = -1;
    memset(b8, '#', CAP);
    in.Buffer = (PWSTR)s; in.Length = (USHORT)(wlen * 2); in.MaximumLength = in.Length;
    d.Buffer = b8; d.Length = 0xBEEF; d.MaximumLength = (USHORT)maxlen;
    r = l_u2u8(&d, &in, FALSE);
    for (k = CAP - 1; k >= 0; --k) if (b8[k] != '#') { last = k; break; }
    printf("    max=%-3d  status %08lX  Length %-6u  bytes touched: %-2d   ",
           maxlen, (unsigned long)r, d.Length, last + 1);
    for (k = 0; k <= last; ++k) printf("%02X ", (unsigned char)b8[k]);
    printf("\n");
}

static void showW(const char* s, int blen, int maxlen)
{
    U8STR in;
    USTR d;
    LONG r;
    int k, last = -1;
    for (k = 0; k < CAP; ++k) bw[k] = 0x2323;
    in.Buffer = (PSTR)s; in.Length = (USHORT)blen; in.MaximumLength = (USHORT)blen;
    d.Buffer = bw; d.Length = 0xBEEF; d.MaximumLength = (USHORT)maxlen;
    r = l_u82u(&d, &in, FALSE);
    for (k = CAP - 1; k >= 0; --k) if (bw[k] != 0x2323) { last = k; break; }
    printf("    max=%-3d  status %08lX  Length %-6u  words touched: %-2d  ",
           maxlen, (unsigned long)r, d.Length, last + 1);
    for (k = 0; k <= last; ++k) printf("%04X ", (unsigned)bw[k]);
    printf("\n");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    static const wchar_t w[] = L"abcdefgh";
    static const char    u[] = "abcdefgh";
    static const wchar_t wm[] = { 0x00E9, 0x20AC, 0xD83D, 0xDE00, 0 };   /* 2 + 3 + 4 bytes */
    static const char    um[] = { (char)0xC3,(char)0xA9, (char)0xE2,(char)0x82,(char)0xAC,
                                  (char)0xF0,(char)0x9F,(char)0x98,(char)0x80, 0 };
    int cap;
    l_u2u8 = (F_U2U8)GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    l_u82u = (F_U82U)GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    if (!l_u2u8 || !l_u82u) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== what a FAILING call leaves behind ==\n");
    printf("   destination poisoned with '#' / U+2323; only the bytes that changed are shown\n\n");

    printf("-- RtlUnicodeStringToUTF8String, 8 ASCII characters (8 bytes + a terminator) --\n");
    for (cap = 0; cap <= 10; ++cap) show8(w, 8, cap);

    printf("\n-- RtlUnicodeStringToUTF8String, e9 / 20ac / a surrogate pair (9 bytes + terminator) --\n");
    for (cap = 0; cap <= 11; ++cap) show8(wm, 4, cap);

    printf("\n-- RtlUTF8StringToUnicodeString, 8 ASCII bytes (16 bytes + a wide terminator) --\n");
    for (cap = 0; cap <= 20; cap += 2) showW(u, 8, cap);
    printf("    -- and the ODD capacities, which cannot hold a whole word --\n");
    for (cap = 1; cap <= 9; cap += 2) showW(u, 8, cap);

    printf("\n-- RtlUTF8StringToUnicodeString, c3a9 / e282ac / f09f9880 (8 bytes out) --\n");
    for (cap = 0; cap <= 12; cap += 2) showW(um, 9, cap);

    printf("\n-- and malformed UTF-8 coming in, which is REPLACED rather than rejected --\n");
    { static const char bad[] = { (char)0x80, 'a', (char)0xE2, (char)0x82, 0 };
      for (cap = 0; cap <= 10; cap += 2) showW(bad, 4, cap); }

    printf("\nREAD THE 'touched' COLUMN. Where it is non-zero on a FAILING status the export wrote\n"
           "part of the result before giving up; where it is zero on a failing status the export\n"
           "decided it would not fit BEFORE converting anything, which a one-pass wrapper cannot do.\n");
    return 0;
}
