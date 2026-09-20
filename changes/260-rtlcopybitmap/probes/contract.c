/* changes/260-rtlcopybitmap/probes/contract.c
 *
 * ntdll!RtlCopyBitMap (RVA 0x13E310) and ntdll!RtlExtractBitMap (RVA 0x1116A0) -- a bit-granular
 * copy between two bitmaps, and its mirror.
 *
 * WHY. discovery/ntdll_bitmap2.c found the largest single anomaly in the whole bitmap family, and
 * it is not a search:
 *
 *      RtlCopyBitMap 65536 bits, target 0                101.05 ns   0.012 ns/byte
 *        ... target 3: every bit shifted                1846.65 ns   0.225 ns/byte
 *
 * EIGHTEEN TIMES, for a target offset of three bits. The aligned copy is `RtlCopyMemory` and runs at
 * memory speed; the shifted one is a loop of about eighteen instructions per 32-BIT WORD, with two
 * variable shifts and TWO read-modify-writes of the destination word:
 *
 *      0013E454  mov rcx, r9          ; the shift, reloaded every iteration
 *      0013E459  and edx, [r11]
 *      0013E461  shl edx, cl
 *      0013E468  mov [r8], eax        ; write the destination word ...
 *      0013E47E  and r13d, [r8]       ; ... and read it back
 *      0013E484  mov [r8], r13d       ; ... and write it again
 *      0013E48A  jne 0013E454
 *
 * RtlExtractBitMap is the same job in the other direction, at 1000.90 ns / 0.123 ns/byte.
 *
 * ------------------------------------------------------------------------------------------------
 * What has to be pinned. These functions mutate, and the interesting half of their contract is
 * about the bits they must NOT touch. Every row below fills the destination with a poison pattern
 * and reports exactly which bytes changed, because "the copy worked" and "the copy worked and also
 * cleared the rest of the word" look identical if only the copied range is examined.
 *
 *   1. Which way does TargetBit apply? Copy reads the source from bit 0 and writes at TargetBit;
 *      Extract reads from TargetBit and writes at bit 0 -- or so the disassembly reads.
 *   2. How many bits? RtlCopyBitMap's fourth argument appears to be ignored -- the count reads as
 *      min(Source->SizeOfBitMap, Destination->SizeOfBitMap - TargetBit), and r9d is overwritten at
 *      0x13E34A before it is ever used. That is a documented three-argument function being called
 *      with four by half the world, so it is worth proving rather than asserting.
 *   3. And what happens when TargetBit is past the destination? The subtraction is done in 32 bits
 *      and then tested in 64, so a negative difference becomes a very large unsigned one. Whether
 *      that turns into a refusal or an enormous copy is exactly the sort of thing an implementation
 *      must match and must not guess.
 *   4. The bits outside the range, in the destination, before and after it.
 *   5. A ZERO-LENGTH copy, and a zero-size source or destination.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef VOID (NTAPI *F_Copy3)(RBM*, RBM*, ULONG);
typedef VOID (NTAPI *F_Copy4)(RBM*, RBM*, ULONG, ULONG);

static F_Copy3 copybm;
static F_Copy4 copybm4, extractbm;

#define NW 16
static ULONG src[NW], dst[NW];
static RBM bs, bd;

static void fill_src_pattern(void)
{
    int i;
    for (i = 0; i < NW; ++i) src[i] = 0x12345678u + (ULONG)i * 0x01010101u;
}

/* report the destination as bytes, and say which ones moved away from the poison */
static void show(const char* what, int nbytes)
{
    int i, changed = 0;
    unsigned char* d = (unsigned char*)dst;
    printf("   %-46s dst:", what);
    for (i = 0; i < nbytes; ++i) printf(" %02X", d[i]);
    for (i = 0; i < NW * 4; ++i) if (d[i] != 0xCC) ++changed;
    printf("   (%d byte(s) changed)\n", changed);
}

static void reset(ULONG dsize, ULONG ssize)
{
    memset(dst, 0xCC, sizeof dst);
    fill_src_pattern();
    bs.SizeOfBitMap = ssize; bs.Buffer = src;
    bd.SizeOfBitMap = dsize; bd.Buffer = dst;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    setvbuf(stdout, NULL, _IONBF, 0);
    copybm    = (F_Copy3)GetProcAddress(h, "RtlCopyBitMap");
    copybm4   = (F_Copy4)GetProcAddress(h, "RtlCopyBitMap");
    extractbm = (F_Copy4)GetProcAddress(h, "RtlExtractBitMap");
    if (!copybm || !extractbm) { printf("resolve failed\n"); return 1; }

    printf("RtlCopyBitMap / RtlExtractBitMap -- the contract\n");
    printf("(the destination is filled with CC; a byte that still reads CC was NOT touched)\n\n");

    /* ---- 1. which way does TargetBit apply? ---- */
    printf("1. WHICH WAY DOES TargetBit APPLY?\n");
    reset(64, 32);
    copybm(&bs, &bd, 0);
    show("COPY src(32 bits) -> dst, target 0", 12);
    reset(64, 32);
    copybm(&bs, &bd, 8);
    show("COPY the same, target 8", 12);
    printf("      => the source is read from BIT 0 and written AT the target if the second row is\n");
    printf("         the first shifted up by one byte\n");
    reset(64, 64);
    extractbm(&bs, &bd, 8, 32);
    show("EXTRACT src from bit 8, 32 bits", 12);
    printf("      => Extract reads AT the target and writes from bit 0 if this is the source\n");
    printf("         shifted DOWN by one byte\n\n");

    /* ---- 2. is RtlCopyBitMap's fourth argument used at all? ---- */
    printf("2. IS RtlCopyBitMap's FOURTH ARGUMENT USED?\n");
    {
        ULONG n;
        static const ULONG NS[4] = { 0, 1, 16, 0xFFFFFFFFu };
        for (n = 0; n < 4; ++n) {
            char label[80];
            reset(64, 32);
            copybm4(&bs, &bd, 0, NS[n]);
            sprintf(label, "COPY target 0, fourth argument = %lu", NS[n]);
            show(label, 8);
        }
        printf("      => IGNORED if all four rows are identical. The count would then be\n");
        printf("         min(Source->SizeOfBitMap, Destination->SizeOfBitMap - TargetBit)\n\n");
    }

    /* ---- 2b. so what IS the count? ---- */
    printf("2b. WHAT IS THE COUNT?\n");
    reset(24, 32);
    copybm(&bs, &bd, 0);
    show("COPY src 32 bits into a dst of only 24", 8);
    printf("      => bounded by the DESTINATION if only 3 bytes changed\n");
    reset(64, 24);
    copybm(&bs, &bd, 0);
    show("COPY src of 24 bits into a dst of 64", 8);
    printf("      => bounded by the SOURCE if only 3 bytes changed\n");
    reset(64, 32);
    copybm(&bs, &bd, 40);
    show("COPY src 32 bits, target 40, dst 64", 12);
    printf("      => bounded by dst_size - target (24 bits) if 3 bytes changed from byte 5\n\n");

    /* ---- 3. a target past the destination ---- */
    printf("3. A TARGET AT OR PAST THE DESTINATION'S SIZE\n");
    printf("   (the difference is computed in 32 bits and tested in 64, so a negative one becomes\n");
    printf("    a very large unsigned value -- this asks what that turns into)\n");
    reset(64, 32);
    copybm(&bs, &bd, 64);
    show("COPY target 64 into a dst of 64", 16);
    reset(64, 32);
    copybm(&bs, &bd, 65);
    show("COPY target 65 into a dst of 64", 16);
    printf("\n");

    /* ---- 4. the bits outside the range ---- */
    printf("4. THE BITS OUTSIDE THE RANGE -- preserved, or clobbered?\n");
    reset(64, 5);
    copybm(&bs, &bd, 3);
    show("COPY 5 bits into bits 3..7 of dst", 4);
    printf("      => bits 0..2 and 8.. must still read CC (11001100) if they are preserved\n");
    reset(64, 5);
    extractbm(&bs, &bd, 3, 5);
    show("EXTRACT 5 bits from bit 3 into dst bit 0", 4);
    printf("\n");

    /* ---- 5. the degenerate sizes ---- */
    printf("5. THE DEGENERATE SIZES\n");
    reset(64, 0);
    copybm(&bs, &bd, 0);
    show("COPY from a source of ZERO bits", 8);
    reset(0, 32);
    copybm(&bs, &bd, 0);
    show("COPY into a destination of ZERO bits", 8);
    reset(64, 32);
    extractbm(&bs, &bd, 0, 0);
    show("EXTRACT zero bits", 8);
    reset(64, 32);
    extractbm(&bs, &bd, 32, 8);
    show("EXTRACT from bit 32 of a 32-bit source", 8);
    printf("\n");

    /* ---- 6. every target offset, so the shift is pinned at all 32 ---- */
    printf("6. EVERY TARGET OFFSET 0..33, copying a 16-bit source\n");
    {
        ULONG t;
        for (t = 0; t <= 33; ++t) {
            char label[80];
            reset(128, 16);
            copybm(&bs, &bd, t);
            sprintf(label, "COPY 16 bits to target %lu", t);
            show(label, 8);
        }
    }

    printf("\nCONTRACT: read the rows above\n");
    return 0;
}
