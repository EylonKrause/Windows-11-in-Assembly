; changes/154-strrev/impl.asm
; char* wia_strrev(char* s)   [Win64: rcx]
;
; Reimplements ucrtbase!_strrev: reverse a NUL-terminated string in place and return the same pointer.
; The live one swaps one byte pair per iteration -- 222 ns for 254 characters, about 4 cycles per
; character, while ucrtbase's own _swab moves bytes at roughly 0.03 ns/byte.
;
; Contract (probed against the live export):
;   - reverses in place, returns the argument pointer unchanged;
;   - nothing past the terminator is touched ("abcde" in a 0x7F-filled buffer leaves 65 64 63 62 61
;     00 7F 7F ...), and the terminator itself stays where it is;
;   - lengths 0 and 1 are no-ops;
;   - NO validation: _strrev(NULL) faults with an access violation and never reaches the
;     invalid-parameter handler. Dereferencing rcx here faults the same way, so that is reproduced by
;     doing nothing special.
;
; Method: an AVX2 strlen, then a two-ended block swap. Each step loads a 32-byte block from each end,
; reverses both (`vpshufb` reverses within each 128-bit lane, `vperm2i128` then swaps the lanes) and
; stores each where the other came from.
;
; The loop deliberately runs while lo <= hi, so the LAST pair of blocks is allowed to OVERLAP. That is
; safe, and the proof is what makes the whole thing branch-light: with lo + hi = n - 32 held invariant,
; storing rev(B) at lo writes s'[lo+k] = s[n-1-lo-k], and storing rev(A) at hi writes
; s'[hi+k] = s[31+lo-k] = s[n-1-hi-k]. Both are exactly s[n-1-j] for the byte they land on, so where
; the two blocks overlap they write identical values and the store order does not matter.
;
; That leaves an odd middle of at most 31 bytes (m = n - 2*lo, which the invariant makes symmetric
; about the centre). m >= 16 is finished by the same overlapping trick one size down, with a pair of
; 16-byte reversals; below that it is at most 7 byte swaps.
;
; ISA: AVX2 (vpshufb, vperm2i128). Validated on Zen3.

.const
ALIGN 16
revmask db 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0

.code
wia_strrev PROC
        mov       r11, rcx                          ; return value: the argument, unchanged

        ; ---- strlen, page-safe -----------------------------------------------------------------
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, ecx                     ; bit i means s[i] == 0
        neg       ecx
        add       ecx, 32                           ; bytes of s covered by the first block
        test      eax, eax
        jnz       sv_len_lo
sv_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       sv_len_hi
        add       rcx, 32
        jmp       sv_next
sv_len_hi:
        tzcnt     eax, eax
        add       rax, rcx
        jmp       sv_have
sv_len_lo:
        tzcnt     eax, eax
sv_have:
        ; rax = n
        cmp       rax, 2
        jb        sv_done                           ; 0 and 1 are no-ops
        mov       r10, r11                          ; lo pointer
        lea       r8, [r11 + rax - 32]              ; hi pointer (may be below lo when n < 32)
        cmp       rax, 32
        jb        sv_middle_setup
        vbroadcasti128 ymm3, xmmword ptr [revmask]

sv_loop:
        cmp       r10, r8
        ja        sv_after                          ; lo > hi -> the ends have crossed
        vmovdqu   ymm0, ymmword ptr [r10]           ; both loads happen before either store, which is
        vmovdqu   ymm2, ymmword ptr [r8]            ;   what makes an overlapping final pair safe
        vpshufb   ymm0, ymm0, ymm3
        vperm2i128 ymm0, ymm0, ymm0, 1
        vpshufb   ymm2, ymm2, ymm3
        vperm2i128 ymm2, ymm2, ymm2, 1
        vmovdqu   ymmword ptr [r10], ymm2
        vmovdqu   ymmword ptr [r8], ymm0
        add       r10, 32
        sub       r8, 32
        jmp       sv_loop

sv_after:
        ; middle = [r10, r10 + m) with m = n - 2*(r10 - s); the invariant keeps it centred
        mov       rcx, r10
        sub       rcx, r11
        neg       rcx
        add       rcx, rcx
        add       rcx, rax                          ; rcx = m = n - 2*lo   (may be <= 0)
        cmp       rcx, 2
        jl        sv_done
        jmp       sv_middle

sv_middle_setup:
        mov       rcx, rax                          ; n < 32: the whole string is the "middle"
sv_middle:
        cmp       rcx, 16
        jb        sv_small
        vmovdqu   xmm0, xmmword ptr [r10]
        vmovdqu   xmm2, xmmword ptr [r10 + rcx - 16]
        vmovdqa   xmm3, xmmword ptr [revmask]
        vpshufb   xmm0, xmm0, xmm3
        vpshufb   xmm2, xmm2, xmm3
        vmovdqu   xmmword ptr [r10], xmm2
        vmovdqu   xmmword ptr [r10 + rcx - 16], xmm0
        jmp       sv_done

sv_small:
        ; at most 7 byte swaps
        lea       r8, [r10 + rcx - 1]
sv_sloop:
        cmp       r10, r8
        jae       sv_done
        movzx     eax, byte ptr [r10]
        movzx     ecx, byte ptr [r8]
        mov       byte ptr [r10], cl
        mov       byte ptr [r8], al
        inc       r10
        dec       r8
        jmp       sv_sloop

sv_done:
        mov       rax, r11
        vzeroupper
        ret
wia_strrev ENDP
END
