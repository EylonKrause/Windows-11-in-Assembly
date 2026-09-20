; changes/266-rtliszeromemory/impl.asm
;   BOOLEAN wia_iszeromemory(const VOID* Buffer, SIZE_T Length)   [Win64: rcx, rdx -> al]
;
; ntdll!RtlIsZeroMemory. discovery/ntdll_bitmap3.c measured it at 1618 ns for 64 KB, 0.025
; ns/byte, about 40 GB/s, where change 259 measured a VPTEST scan of the same shape at 125 GB/s.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than assumed (probes/contract.c):
;
;   * TRUE iff every one of the Length bytes is zero.
;   * Length zero is TRUE, and the pointer is not read at all, a NULL buffer with a zero length
;     answers TRUE rather than faulting.
;   * The length is respected exactly. a byte set one past the end is not seen, at any length from
;     0 to 80, and a byte set at the LAST position is always seen. Nothing is rounded up.
;   * It does not read past the length. Asked at every length from 0 to 300 with the buffer ending
;     exactly at an inaccessible page, the shipped export never faults, so neither may this.
;   * It stops at the first non-zero byte. One megabyte with the non-zero byte first costs 1.55 ns
;     and with it last costs 51424 ns: the early exit is part of the behaviour, not an accident of
;     the buffer, and an implementation that OR-ed the whole buffer together before testing would
;     be thirty thousand times slower on the case the shipped code is fastest at.
;
; ------------------------------------------------------------------------------------------------
; How it works.
;
; VPTEST ymm, ymm sets ZF iff every bit of the register is zero, which is the entire question for
; thirty-two bytes at a time. Four loads are OR-ed together and tested ONCE, so the loop is four
; loads, three ORs and one branch per 128 bytes, but the early exit is still bounded by 128 bytes,
; which is what keeps the "non-zero at the front" case at the floor. Change 259 rejected an
; accumulator across the whole range for exactly this reason; four vectors is the compromise that
; keeps both properties.
;
; Nothing is ever read past the length, and there are two separate mechanisms for that because one
; is not enough:
;
;   * a range of 32 bytes or more finishes with an OVERLAPPING final vector, reading the last 32
;     bytes of the range rather than the next 32 bytes after the cursor. Those bytes have usually
;     been read already, which costs one redundant test and never touches a byte the caller did not
;     offer;
;   * a range SHORTER than 32 bytes never touches a vector register at all, and is read by a ladder
;     of overlapping pairs; the first k bytes and the last k, for k = 8, 4, 2, 1. Every read is
;     strictly inside the range.
;
; That second one is also a speed decision, not only a safety one: a function that has executed a
; VEX instruction must VZEROUPPER before it returns, and change 259 measured that instruction as a
; visible part of a call that only has a few bytes to look at, its short rows sat at 0.75x-0.93x
; until the vector path was made unreachable for them.
;
; ISA: AVX2.

OPTION PROC:PRIVATE
PUBLIC wia_iszeromemory

.code

ALIGN 16
wia_iszeromemory PROC
        ; rcx = buffer, rdx = length
        test      rdx, rdx
        jz        ret_true                       ; length zero: TRUE, and the pointer is never read
        cmp       rdx, 32
        jb        small                          ; ... and no vector register is touched below 32

        lea       r8, [rcx + rdx]                ; one past the end, for the overlapping finish

        ; The first thirty-two bytes, tested alone, before any block loop. The contract has an early
        ; exit; the shipped export answers a 1 MB buffer in 1.55 ns when the non-zero byte is
        ; first, and a loop that ORs four vectors together reads 128 bytes before it can say
        ; anything. That measured 0.90x on exactly that row: slower than the shipped code at the one
        ; thing it is fastest at. The cost of having it is one redundant 32-byte load per call,
        ; which on a megabyte is three thousandths of a percent.
        vmovdqu   ymm0, ymmword ptr [rcx]
        vptest    ymm0, ymm0
        jnz       ret_false_v

        cmp       rdx, 128
        jb        v32

        ; ---- 128 bytes per test: four loads, three ORs, one branch ----
        mov       r9, r8
        sub       r9, 128                        ; the last address a whole 128-byte block may start
ALIGN 16
v128:   vmovdqu   ymm0, ymmword ptr [rcx]
        vpor      ymm0, ymm0, ymmword ptr [rcx + 32]
        vpor      ymm0, ymm0, ymmword ptr [rcx + 64]
        vpor      ymm0, ymm0, ymmword ptr [rcx + 96]
        vptest    ymm0, ymm0
        jnz       ret_false_v                    ; a bit is set somewhere in these 128 bytes
        add       rcx, 128
        cmp       rcx, r9
        jbe       v128

        ; ---- what is left, 32 bytes at a time ----
v32:    mov       r9, r8
        sub       r9, 32                         ; the last address a whole vector may start at
        cmp       rcx, r9
        ja        v_final
ALIGN 16
v32l:   vmovdqu   ymm0, ymmword ptr [rcx]
        vptest    ymm0, ymm0
        jnz       ret_false_v
        add       rcx, 32
        cmp       rcx, r9
        jbe       v32l

v_final:
        ; The last 32 Bytes of the range, which may overlap what was just tested. This is what makes
        ; the tail safe: the alternative is reading 32 bytes from the cursor, which would run past
        ; the caller's buffer and fault at the end of a page.
        vmovdqu   ymm0, ymmword ptr [r9]
        vptest    ymm0, ymm0
        jnz       ret_false_v
        vzeroupper
ret_true:
        mov       eax, 1                         ; a clean 0/1 in eax, not just in al
        ret

ret_false_v:
        vzeroupper
        xor       eax, eax
        ret

; ---- fewer than 32 bytes: a ladder of overlapping reads, no vector register touched ----
;
; Each rung must cover the whole range, and the first draft of this ladder did not. It used one
; overlapping pair (the first 8 bytes and the last 8) for every length from 8 to 31, and a pair
; of k-byte reads only covers a range of n bytes when 2k >= n. At n = 17 byte 8 is in neither half,
; and the corpus that walks a single non-zero byte through every position of every length found it
; at once: 250 mismatches, all of them lengths 17 to 31, all of them ours saying "zero" about a
; buffer that was not. Hence 16 + 16 here, which covers everything up to 32.
small:  cmp       rdx, 16
        jb        lt16
        mov       rax, qword ptr [rcx]
        or        rax, qword ptr [rcx + 8]
        or        rax, qword ptr [rcx + rdx - 16]
        or        rax, qword ptr [rcx + rdx - 8]
        jmp       test_rax
lt16:   cmp       rdx, 8
        jb        lt8
        mov       rax, qword ptr [rcx]
        or        rax, qword ptr [rcx + rdx - 8]
        jmp       test_rax
lt8:    cmp       rdx, 4
        jb        lt4
        mov       eax, dword ptr [rcx]
        or        eax, dword ptr [rcx + rdx - 4]
        jmp       test_rax
lt4:    cmp       rdx, 2
        jb        one
        movzx     eax, word ptr [rcx]
        movzx     r9d, word ptr [rcx + rdx - 2]
        or        eax, r9d
        jmp       test_rax
one:    movzx     eax, byte ptr [rcx]
test_rax:
        test      rax, rax
        setz      al
        movzx     eax, al
        ret
wia_iszeromemory ENDP

END
