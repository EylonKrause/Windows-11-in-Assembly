/* changes/129-rtlchartointeger/probes/pastnul.c
 *
 * DOES ntdll!RtlCharToInteger SKIP ITS OWN TERMINATOR?
 *
 * The leading-skip rule recorded for this export is a SIGNED char compare:
 *
 *      while ((signed char)*s <= ' ') ++s;
 *
 * and 0x00 is <= 0x20. So if that loop has no separate test for the terminator, an all-whitespace string
 * does not end at its NUL -- the scan walks straight through it and keeps going into whatever follows in
 * memory.
 *
 * Our implementation has the separate test and STOPS at the NUL. That is the safe reading, and it is what
 * change 129 has always done. It is also, apparently, not what the export does:
 *
 *   * the live-substitution harness recorded the SHIPPED export returning 6, 7, 3, 5, 0x000000BA,
 *     0x08225F8F and 0xB26A432B for strings whose first byte is NUL, where ours returns 0. Its case
 *     buffer is a reused static array, so the bytes AFTER the terminator are the previous case's;
 *   * probes/nodigits.c, calling with string LITERALS, saw 0 every time -- because the bytes after a
 *     literal "" are whatever the linker put there, and they happened to yield 0.
 *
 * Neither of those proves the rule, because neither controls what follows the NUL. This probe does: it
 * plants a known number after the terminator and asks what comes back. If the answer is that number,
 * the export reads past the end of the string it was given, and a drop-in replacement has to do the same
 * thing to be bit-exact -- which is a far more interesting question than it first looks, because it means
 * matching an OVERREAD.
 *
 * It also finds out how far the scan will go, and whether a guard page stops it, because an export that
 * walks off the end of a page is one a caller can crash.
 *
 * build:  cl /nologo /O2 pastnul.c /Fe:pastnul.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_C2I)(const char*, ULONG, ULONG*);
static F_C2I live;

static void shot(const char* what, const char* s, ULONG base)
{
    ULONG v = 0xA5A5A5A5ul;
    NTSTATUS st;
    int q;
    st = live(s, base, &v);
    printf("   %-34s base %-3lu bytes:", what, base);
    for (q = 0; q < 10; ++q) printf(" %02X", (unsigned char)s[q]);
    printf("  -> st %08lX val %08lX\n", (unsigned long)st, (unsigned long)v);
}

int main(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    static char buf[64];
    int i;

    live = (F_C2I)GetProcAddress(nt, "RtlCharToInteger");
    if (!live) { printf("not exported\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== does RtlCharToInteger scan past its own NUL? ==\n");

    printf("\n-- 1. a terminator, then a number. If the number comes back, the answer is yes.\n");
    memset(buf, 0, sizeof buf);
    buf[0] = 0; strcpy(buf + 1, "6");
    shot("NUL then \"6\"", buf, 10);
    memset(buf, 0, sizeof buf);
    buf[0] = 0; strcpy(buf + 1, "1234");
    shot("NUL then \"1234\"", buf, 10);
    memset(buf, 0, sizeof buf);
    buf[0] = 0; strcpy(buf + 1, "abc");
    shot("NUL then \"abc\"", buf, 16);
    memset(buf, 0, sizeof buf);
    strcpy(buf, "");     strcpy(buf + 4, "99");
    shot("NUL NUL NUL NUL then \"99\"", buf, 10);

    printf("\n-- 2. how many terminators will it walk through? A run of NULs, then a number.\n");
    for (i = 1; i <= 40; i += 8) {
        memset(buf, 0, sizeof buf);
        buf[i] = '7'; buf[i + 1] = 0;
        {
            ULONG v = 0xA5A5A5A5ul;
            NTSTATUS st = live(buf, 10, &v);
            printf("   %2d NUL(s) then '7'   -> st %08lX val %08lX  %s\n",
                   i, (unsigned long)st, (unsigned long)v,
                   v == 7 ? "(walked all of them)" : (v == 0 ? "(stopped)" : "(something else)"));
        }
    }

    printf("\n-- 3. the other 'whitespace' the signed compare eats, mixed with terminators\n");
    memset(buf, 0, sizeof buf);
    buf[0] = 0; buf[1] = ' '; buf[2] = (char)0x80; buf[3] = '\t'; buf[4] = 0; buf[5] = '5'; buf[6] = 0;
    shot("NUL sp 80 tab NUL then \"5\"", buf, 10);
    memset(buf, 0, sizeof buf);
    buf[0] = ' '; buf[1] = 0; buf[2] = '8'; buf[3] = 0;
    shot("sp NUL then \"8\"", buf, 10);
    memset(buf, 0, sizeof buf);
    buf[0] = '-'; buf[1] = 0; buf[2] = '9'; buf[3] = 0;
    shot("'-' NUL then \"9\"  (sign first)", buf, 10);

    printf("\n-- 4. AND WHETHER IT WILL WALK OFF A PAGE. An all-NUL buffer ending exactly at a guard\n");
    printf("      page. If the export faults here, it can be made to fault by any caller who passes\n");
    printf("      a string of nothing but whitespace -- and a replacement that STOPS at the NUL is\n");
    printf("      not bit-exact, but it is also not crashable.\n");
    {
        SYSTEM_INFO si;
        unsigned char* base;
        char* s;
        GetSystemInfo(&si);
        base = (unsigned char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base || !VirtualAlloc(base, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) {
            printf("      allocation failed\n");
            return 0;
        }
        s = (char*)(base + si.dwPageSize) - 8;
        memset(s, 0, 8);                       /* eight NULs, then an unreadable page */
        printf("      calling with 8 NULs ending at the page edge...\n");
        {
            ULONG v = 0xA5A5A5A5ul;
            NTSTATUS st;
            __try {
                st = live(s, 10, &v);
                printf("      returned st %08lX val %08lX -- it did NOT fault\n",
                       (unsigned long)st, (unsigned long)v);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                printf("      *** FAULTED (code %08lX): the shipped export reads past the end of an\n"
                       "          all-whitespace string and off the page ***\n",
                       (unsigned long)GetExceptionCode());
            }
        }
    }
    return 0;
}
