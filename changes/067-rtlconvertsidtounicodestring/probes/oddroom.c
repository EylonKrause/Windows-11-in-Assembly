/* changes/067-rtlconvertsidtounicodestring/probes/oddroom.c
 *
 * AN ODD MaximumLength IS ACCEPTED, AND THE OLD GATE COULD NOT SEE IT.
 *
 * The rebuilt correctness gate swept MaximumLength from 0 to 300 and found four disagreements, all
 * of one shape:
 *
 *     Length 36,  MaximumLength 37   ntdll: SUCCESS       ours and the model: STATUS_BUFFER_OVERFLOW
 *     Length 102, MaximumLength 103  ntdll: SUCCESS       ours and the model: STATUS_BUFFER_OVERFLOW
 *
 * The rule everyone writes down -- and the rule the previous reference.c and impl.asm both
 * implemented -- is "MaximumLength must be at least Length + 2, because the terminator is a wide
 * character". The live export evidently wants only Length + 1.
 *
 * THE OLD GATE STEPPED MaximumLength BY TWO:
 *
 *     for (USHORT ml = 8; ml <= 22; ml += 2) chk(sid, ml);
 *
 * so every odd value -- which is to say the entire boundary -- was skipped, and 3,000,000 further
 * cases all used MaximumLength 600. It is the same defect as change 269's bench row that never
 * reached its own path and change 268's corpus class that could only land on the wrong direction:
 * a test whose generator cannot express the case is not a weak test, it is an absent one.
 *
 * "IT ACCEPTS Length+1" IS NOT YET AN IMPLEMENTATION. A terminator is two bytes and only one is
 * spare, so something has to give, and there are three possibilities that all return SUCCESS:
 *
 *     a. it writes ONE byte of the terminator and leaves the other alone;
 *     b. it writes NO terminator at all;
 *     c. it writes both, one byte past MaximumLength.
 *
 * (c) would be a buffer overrun in ntdll, which is worth knowing either way. This file puts a
 * poison pattern in the destination, calls the export at every MaximumLength around the boundary,
 * and prints the bytes at and after the end of the string -- so the answer is read off rather than
 * reasoned about. It also checks Out->Length, which the caller uses to find the string.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
typedef NTSTATUS (WINAPI *fn)(U*, void*, BOOLEAN);

static unsigned char sid[8 + 4 * 16];
static void mk(unsigned cnt, const unsigned* sub)
{
    unsigned i;
    sid[0] = 1; sid[1] = (unsigned char)cnt;
    for (i = 0; i < 6; ++i) sid[2 + i] = (i == 5) ? 5 : 0;
    for (i = 0; i < cnt; ++i) {
        sid[8 + 4 * i + 0] = (unsigned char)(sub[i]);
        sid[8 + 4 * i + 1] = (unsigned char)(sub[i] >> 8);
        sid[8 + 4 * i + 2] = (unsigned char)(sub[i] >> 16);
        sid[8 + 4 * i + 3] = (unsigned char)(sub[i] >> 24);
    }
}

int main(void)
{
    fn sys = (fn)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlConvertSidToUnicodeString");
    static unsigned sub[16] = { 21, 305419896u, 2596069104u, 287454020u, 1001,
                                7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };
    unsigned counts[] = { 0, 1, 5 };
    unsigned c;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!sys) { printf("resolve failed\n"); return 1; }

    for (c = 0; c < sizeof counts / sizeof counts[0]; ++c) {
        USHORT need;
        int ml;
        mk(counts[c], sub);
        {
            static unsigned char raw[1024];
            U u;
            memset(raw, 0xAA, sizeof raw);
            u.Length = 0; u.MaximumLength = 1000; u.Buffer = (wchar_t*)raw;
            sys(&u, sid, FALSE);
            need = u.Length;
            printf("\n== %u sub-authorities: the string is %u bytes (%u characters) ==\n",
                   counts[c], need, need / 2);
        }
        printf("   MaximumLength  status    Length   the four bytes at [Length-2 .. Length+1]\n");
        for (ml = (int)need - 2; ml <= (int)need + 3; ++ml) {
            static unsigned char raw[1024];
            U u;
            NTSTATUS st;
            if (ml < 0) continue;
            memset(raw, 0xAA, sizeof raw);
            u.Length = 0xBEEF; u.MaximumLength = (USHORT)ml; u.Buffer = (wchar_t*)raw;
            st = sys(&u, sid, FALSE);
            printf("   %10d     %08lX  %6u   %02X %02X %02X %02X   %s\n",
                   ml, (unsigned long)st, u.Length,
                   raw[need - 2], raw[need - 1], raw[need], raw[need + 1],
                   (st == 0 && raw[need] == 0 && raw[need + 1] == 0) ? "a full wide NUL"
                   : (st == 0 && raw[need] == 0) ? "ONE byte of the NUL only"
                   : (st == 0) ? "NO terminator" : "");
            if (st == 0 && ml == (int)need + 1 && raw[need + 1] != 0xAA)
                printf("                  ^^ it wrote past MaximumLength\n");
        }
    }

    printf("\n== and what a caller sees: does Length still describe the string? ==\n");
    {
        static unsigned char raw[1024];
        U u;
        mk(1, sub);
        memset(raw, 0xAA, sizeof raw);
        u.Length = 0xBEEF; u.MaximumLength = 0; u.Buffer = (wchar_t*)raw;
        printf("   MaximumLength 0   -> %08lX, Length %u, first byte %02X\n",
               (unsigned long)sys(&u, sid, FALSE), u.Length, raw[0]);
        memset(raw, 0xAA, sizeof raw);
        u.Length = 0xBEEF; u.MaximumLength = 1; u.Buffer = (wchar_t*)raw;
        printf("   MaximumLength 1   -> %08lX, Length %u, first byte %02X\n",
               (unsigned long)sys(&u, sid, FALSE), u.Length, raw[0]);
    }
    return 0;
}
