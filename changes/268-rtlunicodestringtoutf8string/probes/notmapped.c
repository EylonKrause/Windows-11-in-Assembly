/* changes/268-rtlunicodestringtoutf8string/probes/notmapped.c
 *
 * Does STATUS_SOME_NOT_MAPPED survive the wrapper?
 *
 * The N-forms return 0x00000107 STATUS_SOME_NOT_MAPPED (a SUCCESS code) when they had to
 * substitute U+FFFD for something they could not convert: a lone surrogate going out, malformed
 * UTF-8 coming in. The first draft of this change assumed the wrappers pass that through, and said
 * so in its header. probes/failwrite.c then printed a malformed UTF-8 string coming back through
 * RtlUTF8StringToUnicodeString with the substitution visible in the buffer (fffd 0061 Fffd) and a
 * status of 00000000, which is not what the draft claimed.
 *
 * An assumption that survived into a header comment is exactly the kind of thing that later gets
 * quoted as if it had been measured, so it is measured here instead: both directions, both the
 * caller's-buffer and the allocating forms, with input chosen so that the substitution definitely
 * happens, and the N-form called on the same input alongside, so that what the wrapper did to the
 * status is visible rather than inferred.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG (NTAPI *F_U2U8)(U8STR*, const USTR*, BOOLEAN);
typedef LONG (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);
typedef LONG (NTAPI *F_NU2U8)(char*, ULONG, ULONG*, const wchar_t*, ULONG);
typedef LONG (NTAPI *F_NU82U)(wchar_t*, ULONG, ULONG*, const char*, ULONG);
typedef void (NTAPI *F_FreeU8)(U8STR*);
typedef void (NTAPI *F_FreeU)(USTR*);

static F_U2U8 l_u2u8; static F_U82U l_u82u;
static F_NU2U8 n_u2u8; static F_NU82U n_u82u;
static F_FreeU8 l_freeu8; static F_FreeU l_freeu;

static char    b8[64];
static wchar_t bw[64];

static void out(const char* what, const wchar_t* s, int wlen)
{
    USTR in; U8STR d; LONG r, rn; ULONG produced = 0;
    int k;
    in.Buffer = (PWSTR)s; in.Length = (USHORT)(wlen * 2); in.MaximumLength = in.Length;

    memset(b8, '#', sizeof b8);
    d.Buffer = b8; d.Length = 0xBEEF; d.MaximumLength = 32;
    r = l_u2u8(&d, &in, FALSE);
    rn = n_u2u8(b8 + 40, 20, &produced, s, (ULONG)(wlen * 2));
    printf("  %-34s wrapper %08lX  Length %-3u  N-form %08lX/%-3lu   bytes:",
           what, (unsigned long)r, d.Length, (unsigned long)rn, (unsigned long)produced);
    for (k = 0; k < (int)d.Length && k < 12; ++k) printf(" %02X", (unsigned char)b8[k]);

    memset(&d, 0, sizeof d);
    r = l_u2u8(&d, &in, TRUE);
    printf("   | allocating %08lX  Length %u\n", (unsigned long)r, d.Length);
    if (r >= 0) l_freeu8(&d);
}

static void in16(const char* what, const char* s, int blen)
{
    U8STR in; USTR d; LONG r, rn; ULONG produced = 0;
    int k;
    in.Buffer = (PSTR)s; in.Length = (USHORT)blen; in.MaximumLength = (USHORT)blen;

    for (k = 0; k < 64; ++k) bw[k] = 0x2323;
    d.Buffer = bw; d.Length = 0xBEEF; d.MaximumLength = 64;
    r = l_u82u(&d, &in, FALSE);
    rn = n_u82u(bw + 40, 40, &produced, s, (ULONG)blen);
    printf("  %-34s wrapper %08lX  Length %-3u  N-form %08lX/%-3lu   words:",
           what, (unsigned long)r, d.Length, (unsigned long)rn, (unsigned long)produced);
    for (k = 0; k < (int)(d.Length / 2) && k < 8; ++k) printf(" %04X", (unsigned)bw[k]);

    memset(&d, 0, sizeof d);
    r = l_u82u(&d, &in, TRUE);
    printf("   | allocating %08lX  Length %u\n", (unsigned long)r, d.Length);
    if (r >= 0) l_freeu(&d);
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    l_u2u8 = (F_U2U8)GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    l_u82u = (F_U82U)GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    n_u2u8 = (F_NU2U8)GetProcAddress(h, "RtlUnicodeToUTF8N");
    n_u82u = (F_NU82U)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    l_freeu8 = (F_FreeU8)GetProcAddress(h, "RtlFreeUTF8String");
    l_freeu  = (F_FreeU) GetProcAddress(h, "RtlFreeUnicodeString");
    if (!l_u2u8 || !l_u82u || !n_u2u8 || !n_u82u || !l_freeu8 || !l_freeu) {
        printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== does STATUS_SOME_NOT_MAPPED (00000107) survive the wrapper? ==\n");
    printf("   capacity is generous in every case below, so nothing here is a shortfall\n\n");

    printf("-- UTF-16 -> UTF-8 --\n");
    { static const wchar_t s[] = { L'a', 0xD800, L'b', 0 };  out("a lone HIGH surrogate", s, 3); }
    { static const wchar_t s[] = { L'a', 0xDC00, L'b', 0 };  out("a lone LOW surrogate", s, 3); }
    { static const wchar_t s[] = { 0xD83D, 0xDE00, 0 };      out("a WELL-FORMED pair, for contrast", s, 2); }
    { static const wchar_t s[] = { L'a', L'b', L'c', 0 };    out("plain ASCII, for contrast", s, 3); }

    printf("\n-- UTF-8 -> UTF-16 --\n");
    { static const char s[] = { (char)0x80, 'a', (char)0xE2, (char)0x82 };
      in16("a stray continuation + a truncation", s, 4); }
    { static const char s[] = { (char)0xED, (char)0xA0, (char)0x80 };
      in16("ED A0 80, an encoded surrogate", s, 3); }
    { static const char s[] = { (char)0xFF, (char)0xFE };
      in16("FF FE, never valid", s, 2); }
    { static const char s[] = { (char)0xC3, (char)0xA9 };
      in16("C3 A9, well-formed, for contrast", s, 2); }
    { static const char s[] = { 'a', 'b', 'c' };
      in16("plain ASCII, for contrast", s, 3); }

    printf("\nThe N-form column is the same input through RtlUnicodeToUTF8N / RtlUTF8ToUnicodeN\n"
           "directly, so where the two columns differ the WRAPPER changed the status.\n");
    return 0;
}
