; changes/300-rtlsetallbits/impl.asm
; VOID wia_setallbits(PRTL_BITMAP BitMapHeader)          [rcx]
;
; ntdll!RtlSetAllBits. RTL_BITMAP is { ULONG SizeOfBitMap; PULONG Buffer; }, Buffer at offset 8
; under the x64 layout rules, and the whole routine is one fill:
;
;     memset(Buffer, 0FFh, ((SizeOfBitMap + 31) / 32) * 4)
;
; probes/contract.c established every part of that against the live export rather than assuming it:
; the fill is in whole ULONGs and never ceil(N/8), the padding bits past N inside the final ULONG
; are SET (SizeOfBitMap=1 writes four bytes of FF, not one byte of 01), and SizeOfBitMap=0 writes
; nothing at all, so Buffer is never dereferenced.
;
; WHY THIS IS WORTH CONVERTING, given it is a memset. discovery/bitmap_setall.c timed it against
; RtlClearAllBits, which is the identical function with a different constant, across five sizes:
;
;     bits        SetAll ns   ClrAll ns   Set/Clr
;     64               7.11        2.67      2.67
;     1024            10.44        2.67      3.91
;     65536           72.07       71.62      1.01
;     1048576       1007.91     1023.63      0.98
;
; The two agree from 8 KB up, where both reach ~130 bytes/ns and are plainly running the same wide
; fill. Below that the Set path costs 2.7x to 3.9x what the Clear path costs for the same work, and
; that gap is FIXED overhead rather than a slower loop -- Clear is 2.67 ns at both 8 and 128 bytes,
; so it is not doing anything at all below its threshold, while Set is already paying. Small bitmaps
; are the common case in the allocators that use this, so the small end is the whole change.
;
; The byte count is computed in 64 bits on purpose: N is a ULONG and N + 31 overflows a 32-bit
; register for N > 0FFFFFFE0h.
;
; Registers: rax, rcx, rdx, r8, ymm0 only, all volatile, so there is no prologue and no unwind data
; to get wrong. Every vector op is VEX-encoded, including the 16-byte ones, so the upper YMM halves
; are zeroed by the hardware and no vzeroupper is owed on the paths that never widen.

.code
wia_setallbits PROC
        mov       eax, dword ptr [rcx]                ; N                (zero-extended into rax)
        mov       rcx, qword ptr [rcx + 8]            ; p = Buffer
        add       rax, 31
        shr       rax, 5
        shl       rax, 2                              ; bytes = ((N+31)/32)*4, a multiple of 4
        test      rax, rax
        jz        sab_done                            ; N == 0: Buffer is never touched

        cmp       rax, 32
        jae       sab_big

        ; --- under 32 bytes: 4, 8, 12, 16, 20, 24 or 28, head plus overlapping tail ---
        cmp       rax, 16
        jb        sab_lt16
        vpcmpeqd  xmm0, xmm0, xmm0
        vmovdqu   xmmword ptr [rcx], xmm0
        vmovdqu   xmmword ptr [rcx + rax - 16], xmm0
        ret
sab_lt16:
        cmp       rax, 8
        jb        sab_lt8
        mov       rdx, -1
        mov       qword ptr [rcx], rdx
        mov       qword ptr [rcx + rax - 8], rdx      ; rax is 8 or 12
        ret
sab_lt8:
        mov       dword ptr [rcx], -1                 ; rax is exactly 4
sab_done:
        ret

        ; --- 32 bytes and up ---
sab_big:
        vpcmpeqd  ymm0, ymm0, ymm0
        lea       rdx, [rcx + rax]                    ; end
sab_128:
        lea       r8, [rcx + 128]
        cmp       r8, rdx
        ja        sab_32
        vmovdqu   ymmword ptr [rcx], ymm0
        vmovdqu   ymmword ptr [rcx + 32], ymm0
        vmovdqu   ymmword ptr [rcx + 64], ymm0
        vmovdqu   ymmword ptr [rcx + 96], ymm0
        mov       rcx, r8
        jmp       sab_128
sab_32:
        lea       r8, [rcx + 32]
        cmp       r8, rdx
        ja        sab_tail
        vmovdqu   ymmword ptr [rcx], ymm0
        mov       rcx, r8
        jmp       sab_32
sab_tail:
        ; the remainder is under 32 bytes and the total was at least 32, so a final 32-byte store
        ; ending exactly at the last byte is always in range and never writes past it
        vmovdqu   ymmword ptr [rdx - 32], ymm0
        vzeroupper
        ret
wia_setallbits ENDP
END
