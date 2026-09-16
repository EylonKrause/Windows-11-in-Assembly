/* changes/268-rtlunicodestringtoutf8string/probes/limits.c
 *
 * WHAT HAPPENS WHEN THE RESULT DOES NOT FIT IN A USHORT?
 *
 * Length and MaximumLength are USHORTs. A UNICODE_STRING can be 65534 bytes long; converted to
 * UTF-8 that is up to three bytes per character, and a UTF8_STRING converted the other way is two
 * bytes of UTF-16 per input byte -- either way the result can exceed 65535 and there is nowhere to
 * put it. The interesting question is what the shipped code DOES about that, because there are at
 * least three plausible answers (refuse with a status, truncate the field, or write a wrapped
 * value) and they are not distinguishable by reasoning.
 *
 * It matters for two separate reasons:
 *
 *   1. the CALLER'S-BUFFER path compares a required size against a USHORT capacity, so a 32-bit
 *      comparison and a 16-bit one disagree exactly here;
 *   2. the ALLOCATING path computes an allocation size from that same number, and an implementation
 *      that lets it wrap would allocate a small block and then convert a large string into it.
 *
 * The second is a heap overrun, so this is asked before the code is written rather than after.
 * Both directions, at the sizes where the boundary falls, with the status and both fields printed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG (NTAPI *F_U2U8)(U8STR*, const USTR*, BOOLEAN);
typedef LONG (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);
typedef void (NTAPI *F_FreeU8)(U8STR*);
typedef void (NTAPI *F_FreeU)(USTR*);

static F_U2U8 u2u8; static F_U82U u82u;
static F_FreeU8 freeu8; static F_FreeU freeu;

static wchar_t wsrc[40000];
static char    u8src[70000];
static char    obuf[200000];
static wchar_t owbuf[100000];

/* every character here is three UTF-8 bytes, so n characters need 3n bytes out */
static void out3(int n)
{
    USTR in; U8STR d; LONG st;
    int i;
    for (i = 0; i < n; ++i) wsrc[i] = 0x20AC;
    in.Buffer = wsrc; in.Length = (USHORT)(n * 2); in.MaximumLength = in.Length;
    d.Buffer = obuf; d.Length = 0xBEEF; d.MaximumLength = 0xFFFF;
    st = u2u8(&d, &in, FALSE);
    printf("    %5d chars (%6d bytes out)  caller's buffer -> %08lX  Length=%-6u", n, n * 3,
           (unsigned long)st, d.Length);
    memset(&d, 0, sizeof d);
    st = u2u8(&d, &in, TRUE);
    printf("   allocating -> %08lX  Length=%-6u Max=%-6u\n", (unsigned long)st,
           d.Length, d.MaximumLength);
    if (st >= 0 && freeu8) freeu8(&d);
}

/* A source whose UTF-8 output is EXACTLY `want` bytes.
 *
 * THE SOURCE ITSELF IS LENGTH-LIMITED, which is the trap this function exists to avoid: a
 * UNICODE_STRING holds at most 32767 characters, because Length counts BYTES in a USHORT. A first
 * version of this probe built `want`/2 two-byte characters, which at want = 65535 is 32768
 * characters, whose Length field wrapped to 0 -- and the rows then read as if ntdll had accepted a
 * 65535-byte result, when in fact it had been handed an EMPTY string. So the character count is
 * pinned at the maximum and the byte count is raised by upgrading characters: 32767 characters of
 * ASCII are 32767 bytes, each one promoted to U+00E9 adds one byte and each to U+20AC adds two.
 */
static void outN(int want)
{
    USTR in; U8STR d; LONG st;
    const int n = 32767;
    int extra = want - n, k3, i;
    if (extra < 0 || extra > 2 * n) { printf("    %6d bytes out: not reachable\n", want); return; }
    k3 = extra / 2;
    for (i = 0; i < k3; ++i)      wsrc[i] = 0x20AC;              /* three bytes each */
    if (extra & 1) wsrc[k3++] = 0x00E9;                          /* two bytes */
    for (i = k3; i < n; ++i)      wsrc[i] = L'a';                /* one byte */
    in.Buffer = wsrc; in.Length = (USHORT)(n * 2); in.MaximumLength = in.Length;
    d.Buffer = obuf; d.Length = 0xBEEF; d.MaximumLength = 0xFFFF;
    st = u2u8(&d, &in, FALSE);
    printf("    %6d bytes out  caller's buffer -> %08lX  Length=%-6u", want,
           (unsigned long)st, d.Length);
    memset(&d, 0, sizeof d);
    st = u2u8(&d, &in, TRUE);
    printf("   allocating -> %08lX  Length=%-6u Max=%-6u\n", (unsigned long)st,
           d.Length, d.MaximumLength);
    if (st >= 0 && freeu8) freeu8(&d);
}

/* every byte here is one UTF-16 word, so n bytes need 2n bytes out */
static void in2(int n)
{
    U8STR in; USTR d; LONG st;
    int i;
    for (i = 0; i < n; ++i) u8src[i] = 'a';
    in.Buffer = u8src; in.Length = (USHORT)n; in.MaximumLength = (USHORT)n;
    d.Buffer = owbuf; d.Length = 0xBEEF; d.MaximumLength = 0xFFFF;
    st = u82u(&d, &in, FALSE);
    printf("    %5d bytes (%6d bytes out)  caller's buffer -> %08lX  Length=%-6u", n, n * 2,
           (unsigned long)st, d.Length);
    memset(&d, 0, sizeof d);
    st = u82u(&d, &in, TRUE);
    printf("   allocating -> %08lX  Length=%-6u Max=%-6u\n", (unsigned long)st,
           d.Length, d.MaximumLength);
    if (st >= 0 && freeu) freeu(&d);
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    u2u8 = (F_U2U8)GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    u82u = (F_U82U)GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    freeu8 = (F_FreeU8)GetProcAddress(h, "RtlFreeUTF8String");
    freeu  = (F_FreeU) GetProcAddress(h, "RtlFreeUnicodeString");
    if (!u2u8 || !u82u) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== the USHORT boundary ==\n");
    printf("   MaximumLength is 0xFFFF on the caller's-buffer rows, which is as large as the\n");
    printf("   field goes -- so a shortfall there is a shortfall of the TYPE, not of the buffer\n\n");

    printf("-- UTF-16 -> UTF-8, U+20AC repeated (3 bytes each) --\n");
    out3(21844);      /* 65532 bytes out -- fits, with room for the terminator */
    out3(21845);      /* 65535 bytes out -- fits the field, but the terminator does not */
    out3(21846);      /* 65538 bytes out -- past the field */
    out3(30000);      /* 90000 bytes out -- well past it */

    printf("\n-- UTF-16 -> UTF-8, EXACTLY at the boundary (U+00E9 = 2 bytes, ASCII = 1) --\n");
    { int want;
      for (want = 65530; want <= 65538; ++want) outN(want); }

    printf("\n-- UTF-8 -> UTF-16, ASCII (2 bytes out each) --\n");
    in2(32766);       /* 65532 bytes out -- fits, with room for the wide terminator */
    in2(32767);       /* 65534 bytes out -- fits the field, the terminator does not */
    in2(32768);       /* 65536 bytes out -- past the field */
    in2(40000);       /* 80000 bytes out -- well past it */

    printf("\n== WHICH CHECK WINS when the result is too big for the field AND the destination is\n"
           "   too small as well? The order of the two tests is not observable any other way. ==\n");
    { USTR in; U8STR d; LONG st;
      int i;
      for (i = 0; i < 30000; ++i) wsrc[i] = 0x20AC;
      in.Buffer = wsrc; in.Length = 60000; in.MaximumLength = 60000;
      memset(obuf, '#', 64);
      d.Buffer = obuf; d.Length = 0xBEEF; d.MaximumLength = 4;
      st = u2u8(&d, &in, FALSE);
      printf("   UTF-16 -> UTF-8 : 90000 bytes of result into a 4-byte destination -> %08lX\n"
             "                     Length=%u, first 8 destination bytes:", (unsigned long)st, d.Length);
      for (i = 0; i < 8; ++i) printf(" %02X", (unsigned char)obuf[i]);
      printf("\n");
      d.MaximumLength = 0;
      memset(obuf, '#', 64);
      st = u2u8(&d, &in, FALSE);
      printf("   UTF-16 -> UTF-8 : the same, into a ZERO-capacity destination -> %08lX (capacity 0\n"
             "                     is STATUS_BUFFER_OVERFLOW when the size is legal)\n", (unsigned long)st);
    }
    { U8STR in; USTR d; LONG st;
      int i;
      for (i = 0; i < 40000; ++i) u8src[i] = 'a';
      in.Buffer = u8src; in.Length = 40000; in.MaximumLength = 40000;
      for (i = 0; i < 32; ++i) owbuf[i] = 0x2323;
      d.Buffer = owbuf; d.Length = 0xBEEF; d.MaximumLength = 4;
      st = u82u(&d, &in, FALSE);
      printf("   UTF-8 -> UTF-16 : 80000 bytes of result into a 4-byte destination -> %08lX\n"
             "                     Length=%u, first 8 destination words:", (unsigned long)st, d.Length);
      for (i = 0; i < 8; ++i) printf(" %04X", (unsigned)owbuf[i]);
      printf("\n");
    }

    printf("\nA status that is not a shortfall code on the last rows would mean the field WRAPPED,\n"
           "and the allocating column would then be an allocation of the wrapped size.\n");
    return 0;
}
