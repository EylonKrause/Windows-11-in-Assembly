; ucrtbase.dll!_swab  --  hand-written x86-64 reimplementation (7.91x vs shipped)
; source of truth: changes/145-swab/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/145-swab/impl.asm
; void wia_swab(char* src, char* dest, int n)   [Win64: rcx, rdx, r8d]
;
; Reimplements ucrtbase!_swab: copy n bytes from src to dest with every adjacent byte PAIR swapped.
; ucrtbase's is scalar at ~1 cycle/byte (118 ns for 508 bytes); the whole operation is one `vpshufb`.
;
; Contract (probed against the live export):
;   - floor(n/2) pairs are written; an ODD n leaves the final destination byte untouched (n=7 writes 6
;     bytes, n=1 writes none);
;   - n == 0 writes nothing;
;   - dest == src (fully in place) is fine;
;   - for OVERLAPPING buffers with dest > src it behaves as a strict FORWARD, pair-by-pair copy, so it
;     re-reads bytes it has already written: src="abcdefgh", dest=src+2, n=6 yields "abbaabba". A
;     vector loop cannot reproduce that, so overlap in that direction is detected and handled by a
;     scalar forward loop. (dest < src needs no special case: writes then land below the reads.)
;
; A NEGATIVE n is deliberately NOT reproduced, the live routine runs away and corrupts the stack
; (verified: it faults with STATUS_STACK_BUFFER_OVERRUN). Replicating a buffer overrun is not a goal;
; this returns without writing. That is the one intentional divergence.
;
; ISA: AVX2 (vpshufb). Validated on Zen3.

.const
ALIGN 16
swapmask db 1,0,3,2,5,4,7,6,9,8,11,10,13,12,15,14

.code
wia_swab PROC
        cmp       r8d, 2
        jl        sw_done                           ; n <= 1 (incl. negative): nothing to write
        mov       r9d, r8d
        and       r9d, -2                           ; bytes = n rounded down to a pair
        movsxd    r9, r9d

        ; ---- overlap check: only dest > src can re-read written bytes ----
        cmp       rdx, rcx
        jbe       sw_vec
        mov       rax, rcx
        add       rax, r9
        cmp       rdx, rax
        jae       sw_vec
        ; overlapping forward copy: must match byte-for-byte semantics
sw_ovl:
        movzx     eax, byte ptr [rcx]
        movzx     r10d, byte ptr [rcx + 1]
        mov       byte ptr [rdx], r10b
        mov       byte ptr [rdx + 1], al
        add       rcx, 2
        add       rdx, 2
        sub       r9, 2
        jnz       sw_ovl
        ret

sw_vec:
        vbroadcasti128 ymm1, xmmword ptr swapmask   ; same mask in both 128-bit lanes
        cmp       r9, 32
        jb        sw_16
sw_loop32:
        vmovdqu   ymm0, ymmword ptr [rcx]
        vpshufb   ymm0, ymm0, ymm1                  ; the swap is within 16-byte lanes, so the same
        vmovdqu   ymmword ptr [rdx], ymm0           ; mask serves both halves
        add       rcx, 32
        add       rdx, 32
        sub       r9, 32
        cmp       r9, 32
        jae       sw_loop32
sw_16:
        cmp       r9, 16
        jb        sw_tail
        vmovdqu   xmm0, xmmword ptr [rcx]
        vpshufb   xmm0, xmm0, xmm1
        vmovdqu   xmmword ptr [rdx], xmm0
        add       rcx, 16
        add       rdx, 16
        sub       r9, 16
sw_tail:
        test      r9, r9
        jz        sw_end
sw_pair:
        movzx     eax, byte ptr [rcx]
        movzx     r10d, byte ptr [rcx + 1]
        mov       byte ptr [rdx], r10b
        mov       byte ptr [rdx + 1], al
        add       rcx, 2
        add       rdx, 2
        sub       r9, 2
        jnz       sw_pair
sw_end:
        vzeroupper
sw_done:
        ret
wia_swab ENDP
END
