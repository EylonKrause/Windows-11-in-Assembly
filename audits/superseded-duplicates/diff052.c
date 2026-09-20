/* audits/superseded-duplicates/diff052.c
 *
 * THIS FILE EXISTS BECAUSE IT CAUGHT MY OWN HARNESS, NOT THE CHANGE.
 *
 * abdup.c first reported that change 052 did not agree with live ntdll!RtlIntegerToUnicodeString
 * while change 278 (the same export) agreed exactly. That is a serious claim about a landed
 * change, so before it could be written down it had to be narrowed from a hash to actual cases.
 * This is what it printed:
 *
 *     live  st=00000000 Length=18   0034 0034 0031 0031 0033 0037 0037 0036 0035 0000
 *     052   st=00000000 Length=18   0034 0000 0000 0000 0000 0000 0000 0000 0000 0000
 *
 * The RIGHT length, and an ALL-ZERO buffer. That is not how a formatter fails; it is how an
 * UNINITIALISED TABLE fails. Change 052 keeps its two-digit table in a C file as a bare array plus
 * a wia_dec2_init() that fills it, and its own correctness.c calls that on the first line of main.
 * The audit harness did not. With the initialiser called, 052 matches live and 278 exactly.
 *
 * It is worth keeping the file and the story. This audit is about gates that fail to ask a
 * question; this was a harness that asked one it had not set up for, and produced an equally
 * confident and equally wrong answer. A near-miss report of a defect in landed code is the same
 * class of error as missing a real one, and the thing that caught it was refusing to write down
 * "052 is broken" without first looking at a single failing case.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef NTSTATUS (NTAPI *FP)(ULONG, ULONG, USTR*);

extern NTSTATUS wia_itos(ULONG, ULONG, USTR*);
/* 052 fills its two-digit table at runtime; without this the buffer comes back all
   zeros with a correct Length, which looks exactly like a formatting defect. */
extern void wia_dec2_init(void);

static wchar_t ba[128], bb[128];

int main(void)
{
    FP live = (FP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIntegerToUnicodeString");
    static const ULONG BA[] = { 0, 2, 8, 10, 16, 7 };
    long shown = 0, total = 0, diff = 0;
    long byclass[6] = {0,0,0,0,0,0};
    unsigned long long rs = 0x243F6A8885A308D3ull;
    long iter;

    setvbuf(stdout, NULL, _IONBF, 0);
    wia_dec2_init();
    if (!live) { printf("resolve failed\n"); return 1; }
    printf("== change 052 vs live ntdll!RtlIntegerToUnicodeString ==\n");
    printf("   status, Length, MaximumLength and every byte of the destination\n\n");

    for (iter = 0; iter < 40000; ++iter) {
        USTR ua, ub;
        NTSTATUS ra, rb;
        ULONG v, base;
        USHORT mx;
        int bad;

        rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
        v = (ULONG)rs;
        base = BA[iter % 6];
        mx = (USHORT)(8 + (iter % 90) * 2);

        memset(ba, 0x2A, sizeof ba); memset(bb, 0x2A, sizeof bb);
        ua.Length = 0xBEEF; ua.MaximumLength = mx; ua.Buffer = ba;
        ub.Length = 0xBEEF; ub.MaximumLength = mx; ub.Buffer = bb;
        ra = wia_itos(v, base, &ua);
        rb = live(v, base, &ub);
        ++total;

        bad = (ra != rb) || (ua.Length != ub.Length) || (ua.MaximumLength != ub.MaximumLength)
              || memcmp(ba, bb, sizeof ba) != 0;
        if (!bad) continue;
        ++diff;
        if (ra != rb) byclass[0]++;
        else if (ua.Length != ub.Length) byclass[1]++;
        else byclass[2]++;
        if (base == 7) byclass[3]++;
        if (rb != 0) byclass[4]++;
        if (rb == 0) byclass[5]++;

        if (shown < 10) {
            int i;
            ++shown;
            printf("  v=%-11lu base=%-2lu MaximumLength=%-4u\n", (unsigned long)v,
                   (unsigned long)base, mx);
            printf("     live  st=%08lX Length=%-6u  ", (unsigned long)rb, ub.Length);
            for (i = 0; i < 10; ++i) printf("%04X ", (unsigned short)bb[i]);
            printf("\n     052   st=%08lX Length=%-6u  ", (unsigned long)ra, ua.Length);
            for (i = 0; i < 10; ++i) printf("%04X ", (unsigned short)ba[i]);
            printf("\n\n");
        }
    }
    printf("  %ld of %ld cases differ\n", diff, total);
    printf("    status differs            %ld\n", byclass[0]);
    printf("    Length differs            %ld\n", byclass[1]);
    printf("    only the buffer differs   %ld\n", byclass[2]);
    printf("    ...of which base 7 (invalid)          %ld\n", byclass[3]);
    printf("    ...of which live REFUSED the call     %ld\n", byclass[4]);
    printf("    ...of which live SUCCEEDED            %ld\n", byclass[5]);
    return 0;
}
