; ntdll.dll!RtlCrc32  --  hand-written x86-64 reimplementation (3.03x vs shipped)
; source of truth: changes/267-rtlcrc32/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/267-rtlcrc32/impl.asm
;   ULONG wia_crc32(const void* Buffer, SIZE_T Length, ULONG InitialCrc)
;     [Win64: rcx, rdx, r8d -> eax]
;
; ntdll!RtlCrc32. discovery/ntdll_bitmap3.c measured it at 4627.90 ns for 64 KB -- 0.071 ns/byte,
; the worst per-byte cost found anywhere in ntdll during that sweep.
;
; ------------------------------------------------------------------------------------------------
; Which crc it is, derived rather than guessed (probes/identify.c).
;
; The check value of "123456789" is E3069283, which is CRC-32C's -- Castagnoli, reflected
; polynomial 0x82F63B78 -- and NOT zlib's CBF43926. But an EMPTY buffer returns the third argument
; completely unchanged, which an init and xorout of 0xFFFFFFFF cannot do. Both are true at once if
; the accumulator is complemented on the way IN and again on the way OUT:
;
;       RtlCrc32(p, n, X)  ==  ~CRC32C_raw(p, n, ~X)
;
; confirmed over 5000 random buffers and initial values against a from-scratch bitwise CRC, with
; ZERO disagreements. It is the same shape change 076 found for RtlCrc64, whose accumulator is
; likewise ~Init in and ~crc out -- so the family is consistent, which is worth knowing but was
; still measured here rather than assumed. The third argument is a genuine running CRC: splitting a
; buffer and chaining the calls gives the same answer as one call, over 200 random splits.
;
; ------------------------------------------------------------------------------------------------
; Why it is slow, and what the fix is.
;
; 0x82F63B78 is the polynomial the SSE4.2 CRC32 instruction implements IN HARDWARE, so the shipped
; export is not using a table -- it is using the instruction, serially. CRC32 has 3-cycle latency
; and 1-per-cycle throughput, so a serial chain of `crc32 rax, [mem]` runs at eight bytes per three
; cycles. At this machine's clock that is about 13 GB/s, and the survey measured 14.2. The
; instruction is not the bottleneck; the DEPENDENCY CHAIN through it is.
;
; So the buffer is split into THREE independent chains, which saturates the unit at eight bytes per
; cycle. Splitting is trivial; recombining is the arithmetic. A CRC is linear over GF(2), so for
; three blocks of equal length L:
;
;       CRC(A||B||C) = shift_L( shift_L(crcA) ^ crcB ) ^ crcC
;
; where shift_L advances a CRC past L zero bytes. That is a fixed linear map, tabulated once per L
; in shifttab.c as four 256-entry tables -- four loads and three XORs, twice per 3L bytes. The
; tables are built from the polynomial at run time and then checked against the definition they are
; supposed to satisfy, because they are the one piece of this change that cannot be seen to be
; right by reading it.
;
; Two block sizes, because one leaves a hole. L = 1024 needs 3072 bytes before it can be used at
; all, and a 200-byte buffer would fall all the way back to the serial chain and merely tie the
; shipped code. A second pass with L = 64 covers everything from 192 bytes up. Below that both
; implementations are a serial chain and there is nothing to win.
;
; No frame on the short path. a buffer under 192 bytes is answered by a leaf with no prologue at
; all; the framed body is reached by a tail-jump and its four pushes are paid only by buffers large
; enough not to notice them. That is the shape changes 256, 260 and 262 use.
;
; Reading past the buffer cannot happen: every block loop is entered only when its whole block is
; inside the length, and the tail is 8, then 4, then 2, then 1 byte.
;
; ISA: SSE4.2 (CRC32).

OPTION PROC:PRIVATE
PUBLIC wia_crc32

EXTERN wia_crc32_long_shift:DWORD               ; unsigned long [4][256], built by shifttab.c
EXTERN wia_crc32_short_shift:DWORD

LONGBLK EQU 1024                                ; must match shifttab.c
SHORTBLK EQU 64

.code

; SHIFT -- advance the CRC in eax past one block, through the table whose base is in r8.
; Clobbers r9, rsi, r12.
SHIFT   MACRO
        mov       r9d, eax
        movzx     esi, r9b
        mov       r12d, dword ptr [r8 + rsi*4]
        shr       r9d, 8
        movzx     esi, r9b
        xor       r12d, dword ptr [r8 + 1024 + rsi*4]
        shr       r9d, 8
        movzx     esi, r9b
        xor       r12d, dword ptr [r8 + 2048 + rsi*4]
        shr       r9d, 8
        xor       r12d, dword ptr [r8 + 3072 + r9*4]
        mov       eax, r12d
ENDM

; THREE -- three independent CRC32 chains over three blocks of `blk` bytes starting at rcx, with
; crc0 continuing in rax and crc1/crc2 starting from zero, then recombined through the table in r8.
THREE   MACRO blk
        LOCAL loop3
        xor       r10, r10
        xor       r11, r11
        mov       rsi, rcx
        lea       rdi, [rcx + blk]
        lea       rbx, [rcx + 2*blk]
        mov       r9d, blk / 8
loop3:  crc32     rax, qword ptr [rsi]
        crc32     r10, qword ptr [rdi]
        crc32     r11, qword ptr [rbx]
        add       rsi, 8
        add       rdi, 8
        add       rbx, 8
        dec       r9d
        jnz       loop3
        SHIFT
        xor       eax, r10d
        SHIFT
        xor       eax, r11d
        add       rcx, 3*blk
        sub       rdx, 3*blk
ENDM

; ---------------------------------------------------------------------------------------------
; The entry: a LEAF that answers anything under 192 bytes without a frame, and tail-jumps
; otherwise. eax carries the complemented accumulator into the body.
; ---------------------------------------------------------------------------------------------
ALIGN 16
wia_crc32 PROC
        mov       eax, r8d
        not       eax                           ; the internal accumulator is ~InitialCrc
        cmp       rdx, 3*SHORTBLK
        jae       crc_body
        ; ---- fewer than 192 bytes: one serial chain, no frame, no saved registers ----
        cmp       rdx, 8
        jb        tail_lt8
ALIGN 16
ser8:   crc32     rax, qword ptr [rcx]
        add       rcx, 8
        sub       rdx, 8
        cmp       rdx, 8
        jae       ser8
tail_lt8:
        test      rdx, rdx
        jz        fin
        cmp       rdx, 4
        jb        tail_lt4
        crc32     eax, dword ptr [rcx]
        add       rcx, 4
        sub       rdx, 4
        jz        fin
tail_lt4:
        cmp       rdx, 2
        jb        tail_1
        crc32     eax, word ptr [rcx]
        add       rcx, 2
        sub       rdx, 2
        jz        fin
tail_1: crc32     eax, byte ptr [rcx]
fin:    not       eax
        ret
wia_crc32 ENDP

; ---------------------------------------------------------------------------------------------
ALIGN 16
crc_body PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        .endprolog

        cmp       rdx, 3*LONGBLK
        jb        body_short
        lea       r8, [wia_crc32_long_shift]
ALIGN 16
body_long:
        THREE     LONGBLK
        cmp       rdx, 3*LONGBLK
        jae       body_long

body_short:
        cmp       rdx, 3*SHORTBLK
        jb        body_serial
        lea       r8, [wia_crc32_short_shift]
ALIGN 16
body_sloop:
        THREE     SHORTBLK
        cmp       rdx, 3*SHORTBLK
        jae       body_sloop

body_serial:
        cmp       rdx, 8
        jb        body_lt8
ALIGN 16
body_s8:
        crc32     rax, qword ptr [rcx]
        add       rcx, 8
        sub       rdx, 8
        cmp       rdx, 8
        jae       body_s8
body_lt8:
        test      rdx, rdx
        jz        body_fin
        cmp       rdx, 4
        jb        body_lt4
        crc32     eax, dword ptr [rcx]
        add       rcx, 4
        sub       rdx, 4
        jz        body_fin
body_lt4:
        cmp       rdx, 2
        jb        body_1
        crc32     eax, word ptr [rcx]
        add       rcx, 2
        sub       rdx, 2
        jz        body_fin
body_1: crc32     eax, byte ptr [rcx]
body_fin:
        not       eax
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
crc_body ENDP

END
