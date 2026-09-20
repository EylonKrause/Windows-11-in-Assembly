; changes/209-lstrcpynw/impl.asm
; wchar_t* wia_lstrcpynw_core(wchar_t* dst, const wchar_t* src, int n)   [rcx, rdx, r8d -> rax]
;
; The copy core for kernelbase!lstrcpynW, which measures 3.37 GB/s on a 4000-character copy. There is
; no semantic excuse for that: unlike the two shlwapi routines this project's survey ruled out, there
; is no case folding and no collation here, just a bounded copy that stops at a NUL.
;
; CONTRACT (probes/lcp.c, measured against the live export):
;   * copies at most n-1 characters, stopping early at the source's NUL, then writes ONE terminator.
;     The destination is NOT padded, "ab" into n=10 leaves cells 2..9 untouched, which rules out a
;     strncpy-shaped implementation;
;   * n == 0 writes nothing at all, not even a terminator, and still returns the destination;
;   * n is used UNSIGNED: -1 and -1000 both copy the whole string, they do not mean "empty";
;   * a NULL source or destination returns NULL (handled in seh.c, which owns the argument checks);
;   * and the one that shapes this whole loop, it swallows a faulting source. An unterminated
;     string running into an unmapped page returns NULL, with the characters that WERE readable
;     already sitting in the destination.
;
; That last line is why this loop is page-safe rather than simply wide. a 32-byte load straddling the
; end of a mapped page faults BEFORE storing anything, so a chunked copy would leave FEWER characters
; behind than the shipped byte-at-a-time one does and the partial destination would not match. A
; 16-character chunk is therefore only issued when all sixteen lie inside the current page; near a
; boundary the copy finishes one character at a time, so the fault lands on exactly the character the
; shipped code reaches.
;
; The exception itself is caught in seh.c. On x64 __try/__except is table-driven; it costs nothing
; unless an exception actually fires, so the fast path below is untouched by it.
;
; Only xmm0-xmm5 are touched. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.
;
; ISA: AVX2.

.code
wia_lstrcpynw_core PROC
        ; NOTE: rax is NOT used to hold the destination. The page arithmetic below computes in eax,
        ; and a 32-bit write zero-extends, so stashing the pointer there would silently destroy it --
        ; which it did: the buffer came out correct and only the RETURN VALUE was wrong. rcx is never
        ; modified, so the destination is simply re-read from it at each exit.
        test      r8d, r8d
        jz        done                         ; n == 0 writes nothing whatsoever

        mov       r9d, r8d                     ; n, UNSIGNED: -1 means 4294967295, not "empty"
        dec       r9d                          ; characters we may copy before the terminator
        jz        just_terminate
        xor       r10d, r10d                   ; index, in characters

wide:
        ; No bound test here on purpose: the tail, INCLUDING the one-past read described below --
        ; belongs to the scalar loop. When no characters remain permitted, `permitted` computes to 0
        ; and the chunk test below falls through to it.
        lea       r11, [rdx + r10*2]
        mov       eax, r11d
        and       eax, 4095
        neg       eax
        add       eax, 4096                    ; bytes to the end of this page, 1..4096
        shr       eax, 1                       ; whole characters available without leaving it
        jz        scalar                       ; odd-aligned source with one byte left
        mov       r11d, r9d
        sub       r11d, r10d                   ; characters still permitted
        cmp       eax, r11d
        cmova     eax, r11d                    ; take the smaller of the two limits
        cmp       eax, 16
        jb        scalar

        vmovdqu   ymm0, ymmword ptr [rdx + r10*2]   ; wholly inside the page, by the test above
        vpxor     ymm1, ymm1, ymm1
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r11d, ymm2
        test      r11d, r11d
        jnz       scalar                       ; a NUL in this chunk: finish it precisely below
        vmovdqu   ymmword ptr [rcx + r10*2], ymm0
        add       r10d, 16
        jmp       wide

scalar:
        vzeroupper
s_loop:
        ; The source is read before the bound is tested, and that order is part of the contract, not
        ; a detail. The shipped loop evaluates src[i] first, so with an n-1 exactly equal to the
        ; source length it still reads src[n-1] (one PAST the last character it copies) and an
        ; unterminated string ending at a page boundary faults THERE. probes/pg.c caught this: for
        ; every n == srclen+1 the live export returned NULL with no terminator written, while a
        ; bound-first loop terminated cleanly and returned the destination.
        movzx     r11d, word ptr [rdx + r10*2]
        test      r11d, r11d
        jz        terminate                    ; r10 now indexes the terminator's position
        cmp       r10d, r9d
        jae       terminate                    ; permitted characters exhausted, source still going
        mov       word ptr [rcx + r10*2], r11w
        inc       r10d
        lea       r11, [rdx + r10*2]
        and       r11d, 4095
        jnz       s_loop                       ; only rejoin the wide path at a fresh page start,
        jmp       wide                         ;   where a full chunk is available again

terminate:
        mov       word ptr [rcx + r10*2], 0
        mov       rax, rcx
        vzeroupper
        ret

just_terminate:
        mov       word ptr [rcx], 0
done:
        mov       rax, rcx
        ret
wia_lstrcpynw_core ENDP
END
