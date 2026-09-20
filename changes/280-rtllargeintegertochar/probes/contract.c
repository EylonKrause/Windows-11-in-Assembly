/* changes/280-rtllargeintegertochar/probes/contract.c
 *
 * The 64-BIT formatter's contract, measured one byte at a time.
 *
 * Change 279 established that RtlIntegerToChar's `length` is room in bytes with the terminator
 * written only if it fits, and that a NEGATIVE length is a zero-padded field width. Change 100 --
 * the landed implementation of THIS export -- is wrong on every negative length for exactly the
 * reason 097 was: an unsigned capacity compare, and a corpus that never asked. 123000 of 123000
 * negative-length cases differ from the live export.
 *
 * So this change cannot assume the 64-bit form behaves like the 32-bit one. It has to ask. The two
 * are different exports with different signatures and, as changes 067, 278 and 279 between them
 * showed, three formatters in one DLL can carry two different room rules.
 *
 *     NTSTATUS RtlLargeIntegerToChar(PLARGE_INTEGER value, ulong base, long length, psz string)
 *
 * THE QUESTIONS:
 *
 *   1. The room rule, swept one byte at a time, and whether a terminator is written at exactly
 *      `length == digits`. 279's contract probe printed only twelve bytes and could not see the
 *      end of a nineteen-digit answer, so it could not answer this at all.
 *   2. Is a negative length the same zero-padded field width? Every negative length, against a
 *      guard page, exactly as 279's probes/negative.c did for the 32-bit form.
 *   3. IS INT_MIN the one negative length that refuses here too?
 *   4. SIGNEDNESS. `LARGE_INTEGER` is signed and 279's probe already says -1 prints as
 *      18446744073709551615, but the boundary at 2^63 is asked again here explicitly.
 *   5. THE BASES, and the longest answer in each -- which is what the digit-count tables must span.
 *   6. Is the value read through the pointer more than once? It is passed by pointer, unlike the
 *      32-bit form, so a caller could in principle observe a double read. This decides whether the
 *      implementation may re-read it.
 *   7. What does a refusal leave in the buffer?
 *
 * Nothing is asserted. Every line prints what the live export returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (NTAPI *F_LI2C)(LARGE_INTEGER*, ULONG, LONG, char*);

static F_LI2C li2c;
static char buf[512];

#define POISON '#'

/* print the status and every byte up to the first poison, plus the two bytes after it, so that the
   presence or absence of a terminator at the end of a long answer is visible */
static void show(const char* what, LONG st)
{
    int n = 0, i;
    while (n < 400 && buf[n] != POISON) ++n;
    printf("  %-32s -> %08lX  %2d bytes  \"", what, (unsigned long)st, n);
    for (i = 0; i < n; ++i) putchar(buf[i] ? buf[i] : '.');
    printf("\"  tail:");
    for (i = (n > 2 ? n - 2 : 0); i < n + 3 && i < 400; ++i) printf(" %02X", (unsigned char)buf[i]);
    printf("\n");
}

static void one(const char* what, unsigned long long v, ULONG base, LONG len)
{
    LARGE_INTEGER q;
    q.QuadPart = (long long)v;
    memset(buf, POISON, sizeof buf);
    show(what, li2c(&q, base, len, buf));
}

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    SYSTEM_INFO si;
    unsigned char* base_p;
    SIZE_T pagesz;
    ULONG b;
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    li2c = (F_LI2C)GetProcAddress(hn, "RtlLargeIntegerToChar");
    if (!li2c) { printf("resolve failed\n"); return 1; }

    printf("== 1. THE ROOM RULE AND THE TERMINATOR. \"1234567890123456789\" is nineteen digits ==\n");
    for (k = 16; k <= 24; ++k) {
        char nm[64];
        wsprintfA(nm, "19 digits, length %d", k);
        one(nm, 1234567890123456789ull, 10, k);
    }
    printf("   (a 00 in the tail at length 20 but not at 19 means the rule is change 279's:\n");
    printf("    length >= digits, and the terminator only when there is room for it)\n");

    printf("\n== 1b. the same sweep on the LONGEST decimal answer, twenty digits ==\n");
    for (k = 19; k <= 22; ++k) {
        char nm[64];
        wsprintfA(nm, "20 digits, length %d", k);
        one(nm, 0xFFFFFFFFFFFFFFFFull, 10, k);
    }

    printf("\n== 1c. and on a ONE-digit value, where padding would be most visible ==\n");
    for (k = 0; k <= 5; ++k) {
        char nm[64];
        wsprintfA(nm, "value 7, length %d", k);
        one(nm, 7, 10, k);
    }

    printf("\n== 2. NEGATIVE LENGTHS, with the buffer 96 bytes before a GUARD PAGE ==\n");
    GetSystemInfo(&si);
    pagesz = si.dwPageSize;
    base_p = (unsigned char*)VirtualAlloc(0, pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!base_p || !VirtualAlloc(base_p, pagesz, MEM_COMMIT, PAGE_READWRITE)) {
        printf("   guard page setup failed\n");
    } else {
        printf("   value 1234567890123456789, base 10, which is nineteen characters\n");
        printf("   %8s  %-10s %-8s  %s\n", "length", "status", "faulted", "the first 26 bytes");
        for (k = -16; k >= -26; --k) {
            char* p = (char*)(base_p + pagesz - 96);
            LARGE_INTEGER q;
            LONG st = 0;
            int faulted = 0, i;
            q.QuadPart = 1234567890123456789ll;
            memset(p, POISON, 96);
            __try { st = li2c(&q, 10, (LONG)k, p); }
            __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("   %8d  %08lX   %-8s ", k, (unsigned long)st, faulted ? "FAULT" : "");
            for (i = 0; i < 26; ++i) printf("%02X ", (unsigned char)p[i]);
            printf("\n");
        }

        printf("\n== 3. the far negatives and INT_MIN ==\n");
        {
            static const LONG FAR_[] = { -40, -96, -97, -1000, -2147483647 - 1 };
            unsigned f;
            for (f = 0; f < 5; ++f) {
                char* p = (char*)(base_p + pagesz - 96);
                LARGE_INTEGER q;
                LONG st = 0;
                int faulted = 0, i;
                q.QuadPart = 1234567890123456789ll;
                memset(p, POISON, 96);
                __try { st = li2c(&q, 10, FAR_[f], p); }
                __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
                printf("   %12ld  %08lX   %-8s ", (long)FAR_[f], (unsigned long)st,
                       faulted ? "FAULT" : "");
                for (i = 0; i < 20; ++i) printf("%02X ", (unsigned char)p[i]);
                printf("\n");
            }
            printf("   (-96 exactly fills the buffer; -97 must fault if the width is literal)\n");
        }
    }

    printf("\n== 4. SIGNEDNESS, at the boundary ==\n");
    one("0x7FFFFFFFFFFFFFFF", 0x7FFFFFFFFFFFFFFFull, 10, 40);
    one("0x8000000000000000", 0x8000000000000000ull, 10, 40);
    one("0xFFFFFFFFFFFFFFFF", 0xFFFFFFFFFFFFFFFFull, 10, 40);
    one("-1 as a signed quad", (unsigned long long)-1ll, 10, 40);
    printf("   (if 0x8000... prints 9223372036854775808 it is UNSIGNED, despite the type)\n");

    printf("\n== 5. the bases, and the LONGEST answer in each ==\n");
    for (b = 0; b <= 20; ++b) {
        char nm[64];
        wsprintfA(nm, "base %lu", (unsigned long)b);
        one(nm, 1234567890123456789ull, b, 80);
    }
    one("base 32", 1234567890123456789ull, 32, 80);
    one("base 0xFFFFFFFF", 1234567890123456789ull, 0xFFFFFFFFul, 80);
    printf("\n");
    one("all ones, base 2",  0xFFFFFFFFFFFFFFFFull, 2, 80);
    one("all ones, base 8",  0xFFFFFFFFFFFFFFFFull, 8, 80);
    one("all ones, base 10", 0xFFFFFFFFFFFFFFFFull, 10, 80);
    one("all ones, base 16", 0xFFFFFFFFFFFFFFFFull, 16, 80);
    one("zero, base 2",  0, 2, 80);
    one("zero, base 8",  0, 8, 80);
    one("zero, base 10", 0, 10, 80);
    one("zero, base 16", 0, 16, 80);

    printf("\n== 6. IS THE VALUE READ THROUGH THE POINTER MORE THAN ONCE? ==\n");
    {
        /* Put the LARGE_INTEGER on its own page and make the page unreadable after the call has
           started is not possible single-threaded -- so instead ask the cheaper question: does a
           refused call read it at all? If a bad base refuses WITHOUT touching the value, the
           pointer is dereferenced after validation, and an implementation may do the same. */
        unsigned char* vp = (unsigned char*)VirtualAlloc(0, pagesz, MEM_RESERVE, PAGE_NOACCESS);
        LONG st = 0;
        int faulted = 0;
        if (vp) {
            printf("   a bad base with the value pointer on a NOACCESS page:\n");
            __try { st = li2c((LARGE_INTEGER*)(vp + pagesz - 8), 7, 40, buf); }
            __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("     status %08lX  %s\n", (unsigned long)st,
                   faulted ? "FAULTED -> the value is read BEFORE the base is validated"
                           : "no fault -> the base is validated BEFORE the value is read");
            st = 0; faulted = 0;
            printf("   a good base, no room, with the value pointer on a NOACCESS page:\n");
            __try { st = li2c((LARGE_INTEGER*)(vp + pagesz - 8), 10, 1, buf); }
            __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("     status %08lX  %s\n", (unsigned long)st,
                   faulted ? "FAULTED -> the value is read before the room is known"
                           : "no fault -> the room is checked first");
        }
    }

    printf("\n== 7. what does a REFUSAL leave in the buffer? ==\n");
    {
        LARGE_INTEGER q;
        q.QuadPart = 1234567890123456789ll;
        memset(buf, POISON, sizeof buf);
        printf("   too short:  %08lX, first bytes %02X %02X %02X %02X\n",
               (unsigned long)li2c(&q, 10, 5, buf), (unsigned char)buf[0], (unsigned char)buf[1],
               (unsigned char)buf[2], (unsigned char)buf[3]);
        memset(buf, POISON, sizeof buf);
        printf("   bad base:   %08lX, first bytes %02X %02X %02X %02X\n",
               (unsigned long)li2c(&q, 7, 40, buf), (unsigned char)buf[0], (unsigned char)buf[1],
               (unsigned char)buf[2], (unsigned char)buf[3]);
        memset(buf, POISON, sizeof buf);
        printf("   length 0:   %08lX, first bytes %02X %02X\n",
               (unsigned long)li2c(&q, 10, 0, buf), (unsigned char)buf[0], (unsigned char)buf[1]);
    }
    return 0;
}
