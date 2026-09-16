/* changes/259-rtlarebitsset/probes/contract.c
 *
 * ntdll!RtlAreBitsSet (RVA 0x0F5970) and ntdll!RtlAreBitsClear -- "are all the bits in this range
 * set (or clear)?"
 *
 * WHY THESE TWO. discovery/ntdll_bitmap2.c exists because the FIRST bitmap survey asked this pair a
 * question they could answer immediately: it measured 1.60 ns and 2.20 ns, and both rows returned
 * 0, meaning NO. A range check that answers no stops at the first bit that disagrees, which on
 * those subjects is inside the first word -- those rows timed a two-word function. Asked the
 * expensive question, where the answer is YES and every bit therefore has to be examined:
 *
 *      RtlAreBitsSet   0..60000 over an all-ones bitmap      745.65 ns    0.099 ns/byte
 *      RtlAreBitsClear 0..60000 over an all-zero bitmap      741.05 ns    0.099 ns/byte
 *
 * and the middle loop in the disassembly is SIX INSTRUCTIONS PER 32-BIT WORD:
 *
 *      000F5A05  add rdx, 4        ; the next DWORD
 *      000F5A09  mov eax, [rdx]
 *      000F5A0B  cmp rdx, rbx      ; is this the last one?
 *      000F5A0E  jne 000F5A37
 *      000F5A37  cmp eax, r8d      ; r8d = 0xFFFFFFFF
 *      000F5A3A  je  000F5A05
 *
 * Four bytes per iteration at about two cycles is 0.099 ns/byte, which is what the row says.
 *
 * WHAT HAS TO BE PINNED. The disassembly appears to answer all of it, and that is exactly why it
 * is asked here instead: a rule read out of a branch is a guess about what the branch is for.
 *
 *   1. IS THE SECOND ARGUMENT A LENGTH OR AN END INDEX? `lea r11d, [r8-1] / add r11d, r9d` reads
 *      like start + length - 1, but the same instruction serves an end-exclusive form.
 *   2. LENGTH ZERO. `cmp r8d, 1 / ja main / jne FALSE` reads as "zero is FALSE" -- which is not the
 *      vacuous truth a reader would assume, so it is worth a row of its own.
 *   3. DOES A RANGE PAST THE END REFUSE OR CLAMP? `sub eax, r9d / cmp eax, r8d / jb FALSE` reads as
 *      a refusal, so a range one bit too long over an all-ones bitmap must be FALSE and not TRUE.
 *   4. A START AT OR PAST SizeOfBitMap.
 *   5. THE SLACK past SizeOfBitMap must never be consulted -- a buffer whose bits above the
 *      declared size are all ones must not make a too-long range come back TRUE.
 *   6. THE SINGLE-BIT PATH, which is a separate branch (`bt`) and could disagree with the general
 *      one at the same bit.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef BOOLEAN (NTAPI *F_Are)(RBM*, ULONG, ULONG);

static F_Are are_set, are_clear;
static ULONG buf[64];
static RBM bm;

static void setall(void)  { int i; for (i = 0; i < 64; ++i) buf[i] = 0xFFFFFFFFu; }
static void clrall(void)  { int i; for (i = 0; i < 64; ++i) buf[i] = 0u; }

static void row(const char* what, int set, ULONG start, ULONG len)
{
    BOOLEAN r = set ? are_set(&bm, start, len) : are_clear(&bm, start, len);
    printf("   %-52s %s(%4lu,%4lu) -> %s\n", what, set ? "SET  " : "CLEAR", start, len,
           r ? "TRUE" : "false");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    are_set   = (F_Are)GetProcAddress(h, "RtlAreBitsSet");
    are_clear = (F_Are)GetProcAddress(h, "RtlAreBitsClear");
    if (!are_set || !are_clear) { printf("resolve failed\n"); return 1; }
    bm.Buffer = buf; bm.SizeOfBitMap = 1024;

    printf("RtlAreBitsSet / RtlAreBitsClear -- the contract\n\n");

    /* ---- 1. a length or an end index? ---- */
    printf("1. IS THE SECOND ARGUMENT A LENGTH OR AN END?\n");
    clrall();
    { int i; for (i = 100; i < 110; ++i) buf[i >> 5] |= (1u << (i & 31)); }  /* set 100..109 */
    row("bits 100..109 are set, asked (100,10)", 1, 100, 10);
    row("... asked (100,11): one bit too many",  1, 100, 11);
    row("... asked (100,109): an END index would fit", 1, 100, 109);
    printf("   => a LENGTH if (100,10) is TRUE and (100,109) is false\n\n");

    /* ---- 2. length zero ---- */
    printf("2. LENGTH ZERO -- vacuously true, or refused?\n");
    setall();
    row("all bits set, asked (0,0)",   1, 0, 0);
    row("all bits set, asked (100,0)", 1, 100, 0);
    clrall();
    row("all bits clear, asked (0,0)", 0, 0, 0);
    printf("   => REFUSED if these are false. A caller expecting the empty range to be\n");
    printf("      vacuously true gets the opposite answer\n\n");

    /* ---- 3. a range that runs past the end ---- */
    printf("3. A RANGE PAST SizeOfBitMap -- refused, or clamped to what fits?\n");
    setall();
    bm.SizeOfBitMap = 100;
    row("all ones, size 100, asked (0,100): exactly fits",  1, 0, 100);
    row("... asked (0,101): one bit too many",              1, 0, 101);
    row("... asked (95,10)",                                1, 95, 10);
    printf("   => REFUSED if the too-long ones are false; CLAMPED if they are TRUE\n");
    printf("      (the buffer really is all ones out there, so a clamping form would say TRUE)\n\n");

    /* ---- 4. a start at or past the end ---- */
    printf("4. A START AT OR PAST SizeOfBitMap\n");
    row("all ones, size 100, asked (100,1)", 1, 100, 1);
    row("... asked (101,1)",                 1, 101, 1);
    row("... asked (99,1): the last bit",    1, 99, 1);
    printf("\n");

    /* ---- 5. the slack ---- */
    printf("5. THE SLACK past SizeOfBitMap must never be consulted\n");
    clrall();
    { int i; for (i = 0; i < 40; ++i) buf[i >> 5] |= (1u << (i & 31)); }   /* 0..39 set */
    bm.SizeOfBitMap = 40;
    row("bits 0..39 set, declared 40, asked (0,40)", 1, 0, 40);
    row("... asked (0,41): past the declared size",  1, 0, 41);
    setall();
    bm.SizeOfBitMap = 40;
    row("buffer ALL ones, declared 40, asked (0,41)", 1, 0, 41);
    printf("   => the slack is out of bounds, not just unset: the last row must be false\n\n");

    /* ---- 6. the single-bit path ---- */
    printf("6. THE SINGLE-BIT PATH is separate code (a `bt`), so it is asked separately\n");
    bm.SizeOfBitMap = 1024;
    clrall();
    buf[0] = 0x00000002u;                                  /* bit 1 only */
    row("only bit 1 set, asked (1,1)", 1, 1, 1);
    row("only bit 1 set, asked (0,1)", 1, 0, 1);
    row("only bit 1 set, asked (1,2)", 1, 1, 2);
    row("only bit 1 set, asked (0,1) CLEAR", 0, 0, 1);
    row("only bit 1 set, asked (1,1) CLEAR", 0, 1, 1);
    printf("\n");

    /* ---- 7. a NULL bitmap, since every other change in this family asks ---- */
    printf("7. Not asked here: a NULL RTL_BITMAP. The shipped code dereferences rcx on its first\n");
    printf("   instruction, so there is no contract to match -- only a fault to reproduce.\n");

    printf("\nCONTRACT: read the rows above\n");
    return 0;
}
