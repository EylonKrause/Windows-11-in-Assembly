/* changes/261-rtlfindnextforwardrunclear/probes/contract.c
 *
 * ntdll!RtlFindNextForwardRunClear (RVA 0x0DB350) and ntdll!RtlFindLastBackwardRunClear -- "the
 * next run of clear bits at or after this index", and "the last one at or before it".
 *
 * WHY. discovery/ntdll_bitmap2.c timed both for the first time:
 *
 *      RtlFindNextForwardRunClear from 1                 400.95 ns   0.100 ns/byte
 *      RtlFindLastBackwardRunClear from 65535            420.30 ns   0.053 ns/byte
 *
 * and the forward scan is seven instructions per 32-BIT word:
 *
 *      000DB3B0  not r10d
 *      000DB3B3  test r10d, r10d
 *      000DB3B6  jne found
 *      000DB3B8  cmp rcx, r9        ; past the end?
 *      000DB3BB  ja  done
 *      000DB3BD  mov r10d, [rax+4]
 *      000DB3C1  add rax, 4
 *      000DB3C5  add rcx, 4
 *      000DB3C9  jmp 000DB3B0
 *
 * Four bytes per iteration at about two cycles is the 0.100 the row reports. One VPCMPEQD looks at
 * thirty-two.
 *
 * ------------------------------------------------------------------------------------------------
 * What has to be pinned. Both return a length and write a start through a pointer, and the
 * interesting questions are all about what happens at the edges of the search.
 *
 *   1. If FromIndex is already inside a clear run, does the answer start at FromIndex or at the
 *      run's true beginning? The two are different by as much as the run is long, and a caller
 *      walking a bitmap with the result would loop forever on one reading and not the other.
 *   2. The backward form: is FromIndex included, and does it report the run it lands in or the last
 *      one strictly before it? And does the length run to FromIndex or to the run's true end?
 *   3. nothing FOUND: what length, and is the start pointer written at all? A caller that trusts an
 *      untouched pointer reads whatever it happened to contain.
 *   4. FromIndex at or past SizeOfBitMap.
 *   5. a run that reaches the end of the bitmap -- the slack past SizeOfBitMap must not extend it.
 *   6. An entirely clear and an entirely set bitmap, and SizeOfBitMap = 0.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Run)(RBM*, ULONG, PULONG);

static F_Run fwd, back;
static ULONG buf[32];
static RBM bm;

static void allset(void)  { int i; for (i = 0; i < 32; ++i) buf[i] = 0xFFFFFFFFu; }
static void allclr(void)  { int i; for (i = 0; i < 32; ++i) buf[i] = 0u; }
static void crun(int s, int n) { int i; for (i = s; i < s + n; ++i) buf[i >> 5] &= ~(1u << (i & 31)); }

static void row(const char* what, int backward, ULONG from)
{
    ULONG start = 0xDEADBEEFu, len;
    len = backward ? back(&bm, from, &start) : fwd(&bm, from, &start);
    printf("   %-52s %s(%5lu) -> len=%-6lu start=%s%lu\n", what, backward ? "BACK" : "FWD ", from,
           len, (start == 0xDEADBEEFu) ? "UNTOUCHED " : "", start);
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    fwd  = (F_Run)GetProcAddress(h, "RtlFindNextForwardRunClear");
    back = (F_Run)GetProcAddress(h, "RtlFindLastBackwardRunClear");
    if (!fwd || !back) { printf("resolve failed\n"); return 1; }
    bm.Buffer = buf; bm.SizeOfBitMap = 1024;

    printf("RtlFindNextForwardRunClear / RtlFindLastBackwardRunClear -- the contract\n");
    printf("(start is printed as UNTOUCHED when the call left the caller value alone)\n\n");

    /* ---- 1. FromIndex inside a run ---- */
    printf("1. FromIndex ALREADY INSIDE A CLEAR RUN\n");
    allset(); crun(100, 20);                    /* clear 100..119 */
    row("clear 100..119, asked from 0",   0, 0);
    row("... from 100 (the first bit of it)", 0, 100);
    row("... from 105 (the middle of it)",    0, 105);
    row("... from 119 (its last bit)",        0, 119);
    row("... from 120 (just past it)",        0, 120);
    printf("      => the run is reported FROM FromIndex if (105) gives start=105 len=15,\n");
    printf("         and from its true beginning if it gives start=100 len=20\n\n");

    /* ---- 2. the backward form ---- */
    printf("2. THE BACKWARD FORM\n");
    row("clear 100..119, asked back from 1023", 1, 1023);
    row("... back from 120 (just past it)",     1, 120);
    row("... back from 119 (its last bit)",     1, 119);
    row("... back from 105 (the middle of it)", 1, 105);
    row("... back from 100 (its first bit)",    1, 100);
    row("... back from 99 (just before it)",    1, 99);
    printf("      => is FromIndex itself included, and does the length reach FromIndex or the\n");
    printf("         run's true end?\n\n");

    /* ---- 3. nothing found ---- */
    printf("3. NOTHING FOUND -- what length, and is the start pointer written?\n");
    allset();
    row("no clear bits at all, forward from 0",  0, 0);
    row("no clear bits at all, backward from 1023", 1, 1023);
    allset(); crun(10, 4);
    row("clear 10..13 only, forward from 100",   0, 100);
    row("clear 10..13 only, backward from 5",    1, 5);
    printf("\n");

    /* ---- 4. FromIndex at or past the size ---- */
    printf("4. FromIndex AT OR PAST SizeOfBitMap\n");
    allset(); crun(1000, 24);                   /* clear 1000..1023, to the very end */
    row("clear 1000..1023, forward from 1024",  0, 1024);
    row("... forward from 1025",                0, 1025);
    row("... backward from 1024",               1, 1024);
    row("... backward from 2000",               1, 2000);
    printf("\n");

    /* ---- 5. a run reaching the end, and the slack ---- */
    printf("5. A RUN THAT REACHES THE END -- the slack past SizeOfBitMap must not extend it\n");
    row("clear 1000..1023, size 1024, forward from 900", 0, 900);
    bm.SizeOfBitMap = 1010;
    row("... the same buffer declared as 1010",          0, 900);
    printf("      => the length must be 10, not 24, in the second row\n");
    bm.SizeOfBitMap = 1024;
    row("... and backward from 1023",                    1, 1023);
    printf("\n");

    /* ---- 6. the degenerate bitmaps ---- */
    printf("6. THE DEGENERATE BITMAPS\n");
    allclr();
    row("entirely clear, forward from 0",     0, 0);
    row("entirely clear, forward from 500",   0, 500);
    row("entirely clear, backward from 1023", 1, 1023);
    allset();
    bm.SizeOfBitMap = 0;
    row("SizeOfBitMap = 0, forward from 0",   0, 0);
    row("SizeOfBitMap = 0, backward from 0",  1, 0);
    bm.SizeOfBitMap = 1;
    allclr();
    row("a ONE-BIT bitmap, clear, forward from 0",  0, 0);
    row("a ONE-BIT bitmap, clear, backward from 0", 1, 0);
    allset();
    row("a ONE-BIT bitmap, set, forward from 0",    0, 0);

    printf("\nCONTRACT: read the rows above\n");
    return 0;
}
