/* changes/268-rtlunicodestringtoutf8string/probes/statuses.c
 *
 * The exact status and the exact destination state, for every size relationship.
 *
 * probes/contract.c showed three different outcomes already, STATUS_SUCCESS, STATUS_BUFFER_TOO_SMALL
 * (0xC0000023) and STATUS_BUFFER_OVERFLOW (0x80000005), and they do not line up with a single
 * "does it fit" test:
 *
 *      "abc" (3 UTF-8 bytes) into MaximumLength 3  ->  0xC0000023
 *      "abc"                 into MaximumLength 4  ->  SUCCESS, and a NUL is written
 *      an EMPTY source       into MaximumLength 0  ->  0x80000005, not 0xC0000023
 *
 * so the terminator needs room of its own, and the zero-capacity case is its own outcome. A
 * reimplementation that returned the wrong one of those two failure codes would look right in every
 * test that only asked "did it fail".
 *
 * Also: On failure the destination is partially written, "abc" into MaximumLength 2 leaves an 'a'
 * behind, and Length is NOT updated. That is the N-form's own behaviour showing through, and it
 * has to be reproduced rather than tidied up, because a caller that inspects the buffer after a
 * failure sees it.
 *
 * This file sweeps every capacity from 0 to 12 against outputs of 0 to 6 bytes, in both directions,
 * and prints the status, the resulting Length and the first bytes of the destination. It is the
 * table an implementation has to match.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG (NTAPI *F_U2U8)(U8STR*, const USTR*, BOOLEAN);
typedef LONG (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);

static F_U2U8 u2u8;
static F_U82U u82u;
static char    adst[64];
static wchar_t wdst[64];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int nsrc, cap;
    u2u8 = (F_U2U8)GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    u82u = (F_U82U)GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    if (!u2u8 || !u82u) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== UTF-16 -> UTF-8: every source length against every capacity ==\n");
    printf("   rows are the source length in CHARACTERS (so the UTF-8 output is the same number of\n");
    printf("   bytes, all ASCII); columns are MaximumLength. Each cell is the status, then the\n");
    printf("   Length the call left behind (P = the poison 0xBEEF, i.e. not written).\n\n");
    printf("      cap: ");
    for (cap = 0; cap <= 8; ++cap) printf("      %2d    ", cap);
    printf("\n");
    for (nsrc = 0; nsrc <= 5; ++nsrc) {
        wchar_t src[8];
        USTR in;
        int i;
        for (i = 0; i < nsrc; ++i) src[i] = (wchar_t)(L'a' + i);
        in.Buffer = src; in.Length = (USHORT)(nsrc * 2); in.MaximumLength = in.Length;
        printf("  n=%d:  ", nsrc);
        for (cap = 0; cap <= 8; ++cap) {
            U8STR out;
            LONG st;
            memset(adst, '#', sizeof adst);
            out.Buffer = adst; out.Length = 0xBEEF; out.MaximumLength = (USHORT)cap;
            st = u2u8(&out, &in, FALSE);
            printf("%08lX/%-3s", (unsigned long)st,
                   out.Length == 0xBEEF ? "P" : (out.Length == 0 ? "0" :
                   (out.Length == 1 ? "1" : (out.Length == 2 ? "2" :
                   (out.Length == 3 ? "3" : (out.Length == 4 ? "4" : "?"))))));
        }
        printf("\n");
    }

    printf("\n   and what the destination looks like afterwards, for n=3:\n");
    {
        wchar_t src[4] = { L'a', L'b', L'c', 0 };
        USTR in;
        in.Buffer = src; in.Length = 6; in.MaximumLength = 6;
        for (cap = 0; cap <= 6; ++cap) {
            U8STR out;
            LONG st;
            int i;
            memset(adst, '#', sizeof adst);
            out.Buffer = adst; out.Length = 0xBEEF; out.MaximumLength = (USHORT)cap;
            st = u2u8(&out, &in, FALSE);
            printf("     cap=%d -> %08lX  buf=[", cap, (unsigned long)st);
            for (i = 0; i < 8; ++i) printf("%c", adst[i] ? (adst[i] == '#' ? '#' : adst[i]) : '.');
            printf("]\n");
        }
    }

    printf("\n== UTF-8 -> UTF-16: the same sweep ==\n");
    printf("      cap: ");
    for (cap = 0; cap <= 12; cap += 2) printf("      %2d    ", cap);
    printf("\n");
    for (nsrc = 0; nsrc <= 5; ++nsrc) {
        char src[8];
        U8STR in;
        int i;
        for (i = 0; i < nsrc; ++i) src[i] = (char)('a' + i);
        in.Buffer = src; in.Length = (USHORT)nsrc; in.MaximumLength = (USHORT)nsrc;
        printf("  n=%d:  ", nsrc);
        for (cap = 0; cap <= 12; cap += 2) {
            USTR out;
            LONG st;
            memset(wdst, 0x23, sizeof wdst);
            out.Buffer = wdst; out.Length = 0xBEEF; out.MaximumLength = (USHORT)cap;
            st = u82u(&out, &in, FALSE);
            printf("%08lX/%-3u", (unsigned long)st,
                   out.Length == 0xBEEF ? 999u : (unsigned)out.Length);
        }
        printf("\n");
    }
    printf("\n   (999 in the Length column means the field was left at its poison value)\n");
    return 0;
}
