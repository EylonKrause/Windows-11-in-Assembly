/* changes/280-rtllargeintegertochar/probes/overrun.c
 *
 * What happens when a field width is longer than the caller's buffer.
 *
 * probes/contract.c turned up something that needs pinning down before it can be written up. With
 * the buffer ending exactly at a guard page:
 *
 *     length  -96  (fills the buffer exactly)   -> STATUS_SUCCESS
 *     length  -97  (one byte past)              -> RETURNED C0000005, no exception reached us
 *     length -1000 (far past)                   -> raised an exception our __except caught
 *
 * Those two cannot both be "it just writes off the end". A returned STATUS_ACCESS_VIOLATION means
 * something inside ntdll caught the fault and turned it into a status; an escaping exception means
 * nothing did. If the export really does catch faults, that is a contract element, and an
 * implementation that faults instead would differ from it in a way a caller could see.
 *
 * So: sweep the overrun distance from one byte to far past, printing for each whether the call
 * Returned a status or raised, and compare the shipped export against both implementations -- the
 * landed change 100 and this change's -- on identical setups.
 *
 * This regime is OUTSIDE the domain any gate here tests, because a field width longer than the
 * buffer is a caller bug and the corpus must not contain one. The point of this probe is to know
 * exactly what is and is not being reproduced, and to say so in RESULTS.md rather than leave it as
 * an unexamined difference.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (NTAPI *F)(LARGE_INTEGER*, ULONG, LONG, char*);
extern LONG wia_litoc(LARGE_INTEGER*, ULONG, LONG, char*);            /* change 100 */
extern LONG wia_lint2char(const LARGE_INTEGER*, ULONG, LONG, char*);  /* change 280 */

static F live;
static unsigned char* base_p;
static SIZE_T pagesz;

/* room = how many bytes of committed page the buffer gets before the guard page */
static void ask(const char* who, int which, int room, LONG len)
{
    char* p = (char*)(base_p + pagesz - room);
    LARGE_INTEGER q;
    LONG st = 0;
    int raised = 0;
    DWORD code = 0;

    q.QuadPart = 1234567890123456789ll;
    memset(p, '#', room);
    __try {
        st = which == 0 ? live(&q, 10, len, p)
           : which == 1 ? wia_litoc(&q, 10, len, p)
                        : wia_lint2char(&q, 10, len, p);
    }
    __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { raised = 1; }

    printf("   %-12s room %4d  width %5ld  ", who, room, (long)-len);
    if (raised) printf("RAISED  %08lX\n", (unsigned long)code);
    else        printf("returned %08lX\n", (unsigned long)st);
}

int main(void)
{
    SYSTEM_INFO si;
    static const int OVER[] = { 0, 1, 2, 8, 16, 32, 64, 128, 512, 4096 };
    unsigned i;

    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlLargeIntegerToChar");
    if (!live) { printf("resolve failed\n"); return 1; }
    GetSystemInfo(&si);
    pagesz = si.dwPageSize;
    base_p = (unsigned char*)VirtualAlloc(0, pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!base_p || !VirtualAlloc(base_p, pagesz, MEM_COMMIT, PAGE_READWRITE)) {
        printf("guard page setup failed\n"); return 1;
    }

    printf("== a field width that runs past the buffer, by a controlled number of bytes ==\n");
    printf("   the buffer always ends exactly at a guard page; 'width' is -length\n\n");
    for (i = 0; i < sizeof OVER / sizeof OVER[0]; ++i) {
        int room = 96;
        LONG len = -(LONG)(room + OVER[i]);
        printf("   -- %d byte(s) past the end --\n", OVER[i]);
        ask("live ntdll", 0, room, len);
        ask("change 100", 1, room, len);
        ask("change 280", 2, room, len);
    }
    return 0;
}
