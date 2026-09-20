/* changes/279-rtlintegertochar/probes/negative.c
 *
 * a negative `length` does something, and it is not nothing.
 *
 * probes/contract.c swept `length` from 0 to 14 and got a clean rule: it is room in BYTES, the call
 * needs `length >= digits`, and a terminator is written only if `length > digits`. That is change
 * 067's rule for RtlConvertSidToUnicodeString, and NOT change 278's for
 * RtlIntegerToUnicodeString, which demands Length+2 always. Three formatters in one DLL, two rules.
 *
 * Then it asked the two values the parameter's type allows but nobody passes:
 *
 *     length  -1     -> STATUS_BUFFER_OVERFLOW, buffer untouched
 *     length -100    -> STATUS_SUCCESS, and the buffer starts "00"
 *
 * For the value 3735928559 that "00" is not the answer to anything. Two negatives, two different
 * outcomes, and one of them a success that wrote something wrong, so `length` is not simply being
 * compared as signed, and it is not simply being cast to unsigned either, because -1 as unsigned is
 * the largest possible room and would have succeeded.
 *
 * This matters because the gate compares against live. If the behaviour is deterministic, the
 * implementation has to reproduce it. If it is an out-of-bounds write, the corpus must not contain
 * it and this file is the record of why. Either way the answer has to be measured before a line is
 * written, guessing is what changes 067 and 278 were each caught by.
 *
 * So: every negative length from -1 to -40, and a few far ones, with the buffer poisoned and a
 * GUARD PAGE immediately after it, so that a write past the end is a fault this probe survives
 * rather than damage it does not notice.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (NTAPI *F_I2C)(ULONG, ULONG, LONG, char*);

static F_I2C i2c;

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pagesz;
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    i2c = (F_I2C)GetProcAddress(hn, "RtlIntegerToChar");
    if (!i2c) { printf("resolve failed\n"); return 1; }

    GetSystemInfo(&si);
    pagesz = si.dwPageSize;
    base = (unsigned char*)VirtualAlloc(0, pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!base || !VirtualAlloc(base, pagesz, MEM_COMMIT, PAGE_READWRITE)) {
        printf("guard page setup failed\n"); return 1;
    }

    printf("== every negative length, with the buffer 64 bytes before a guard page ==\n");
    printf("   value 3735928559, base 10, which is ten characters\n");
    printf("   %8s  %-10s %-8s  %s\n", "length", "status", "faulted", "the first 16 bytes");
    for (k = -1; k >= -40; --k) {
        char* buf = (char*)(base + pagesz - 64);
        LONG st = 0;
        int faulted = 0, i;
        memset(buf, '#', 64);
        __try { st = i2c(3735928559ul, 10, (LONG)k, buf); }
        __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   %8d  %08lX   %-8s ", k, (unsigned long)st, faulted ? "FAULT" : "");
        for (i = 0; i < 16; ++i) printf("%02X ", (unsigned char)buf[i]);
        printf("\n");
        if (k < -12) break;                    /* the pattern is established by then */
    }

    printf("\n== the far negatives ==\n");
    {
        static const LONG FAR_[] = { -100, -1000, -65536, -2147483647 - 1 };
        unsigned f;
        for (f = 0; f < 4; ++f) {
            char* buf = (char*)(base + pagesz - 64);
            LONG st = 0;
            int faulted = 0, i;
            memset(buf, '#', 64);
            __try { st = i2c(3735928559ul, 10, FAR_[f], buf); }
            __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("   %12ld  %08lX   %-8s ", (long)FAR_[f], (unsigned long)st,
                   faulted ? "FAULT" : "");
            for (i = 0; i < 16; ++i) printf("%02X ", (unsigned char)buf[i]);
            printf("\n");
        }
    }

    printf("\n== is it deterministic? the same call ten times ==\n");
    {
        char* buf = (char*)(base + pagesz - 64);
        int i, j;
        for (j = 0; j < 10; ++j) {
            LONG st;
            memset(buf, '#', 64);
            st = i2c(3735928559ul, 10, -100, buf);
            printf("   run %d: %08lX  ", j, (unsigned long)st);
            for (i = 0; i < 8; ++i) printf("%02X ", (unsigned char)buf[i]);
            printf("\n");
        }
    }

    printf("\n== and with a different VALUE, to see what it is actually writing ==\n");
    {
        static const ULONG V[] = { 0, 1, 7, 42, 999, 3735928559ul, 4294967295ul };
        unsigned f;
        for (f = 0; f < 7; ++f) {
            char* buf = (char*)(base + pagesz - 64);
            LONG st;
            int i;
            memset(buf, '#', 64);
            st = i2c(V[f], 10, -100, buf);
            printf("   value %-12lu %08lX  ", (unsigned long)V[f], (unsigned long)st);
            for (i = 0; i < 16; ++i) printf("%02X ", (unsigned char)buf[i]);
            printf("\n");
        }
        printf("   (if the bytes do not depend on the value, nothing is being formatted at all)\n");
    }
    return 0;
}
