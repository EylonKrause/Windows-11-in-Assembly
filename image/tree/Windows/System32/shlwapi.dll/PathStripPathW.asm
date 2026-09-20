; shlwapi.dll!PathStripPathW  --  hand-written x86-64 reimplementation (3.28x vs shipped)
; source of truth: changes/162-pathstrippathw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/162-pathstrippathw/impl.asm
; void wia_pathstrippathw(PWSTR pszPath)   [Win64: rcx]
;
; Reimplements shlwapi!PathStripPathW: remove the directory portion of a path in place, leaving only
; the last component. 82 ns for a ~90-character path.
;
; This is change 161 plus a move, and that equivalence was verified rather than assumed: over every
; string in {a, backslash, slash, colon} up to length 8 -- 87381 of them -- the buffer left by the
; live PathStripPathW is byte for byte what you get by copying the live PathFindFileNameW result to
; the front. So the separator rule is the one derived in 161:
;
;   * '\' and '/' are always separators, each moving the answer past itself when the next character
;     is neither NUL nor '\' nor '/';
;   * ':' does the same only when it is the SOLE colon in its run, a run being the stretch between
;     two backslash/slash characters;
;   * the answer is the last position that set, otherwise the start.
;
; The live one also leaves the bytes PAST the new terminator untouched -- stripping "C:\dir\file.txt"
; leaves "file.txt\0" followed by the stale tail "le.txt\0" -- so this is a plain forward copy with
; no zero fill, and the correctness harness compares the whole buffer to prove it.
;
; The copy is forward and the destination is strictly below the source, so overlap is safe: each
; 32-byte block is loaded into a register before the store, and the store lands entirely behind the
; next block's read. The usual head/tail overlapping trick would NOT be safe here for the same
; reason, so the remainder walks down 16/8/4/2 instead.
;
; ISA: AVX2 + BMI1/BMI2. Validated on Zen3.

; Only ymm0-ymm5 may be used. xmm6-xmm15 are callee-saved under Win64 -- their low 128 bits are --
; so parking the ':' constant in ymm6, as an earlier cut did, silently destroyed any double the
; caller had live. Invisible to a correctness test, which compares pointers and characters.
; See tools/abi-check. ':' is the rarest of the four separators and is only compared against, so it
; becomes a memory operand and the register disappears; the other three stay in registers.
.const
ALIGN 16
c_bsl   dw 005Ch
c_sla   dw 002Fh
c_col   dw 003Ah
; 32-byte form, for use as a memory operand. VEX operands need no alignment, so no ALIGN 32
; (which .const rejects with A2189).
c_colm  dw 16 dup(003Ah)

.code
wia_pathstrippathw PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        .endprolog

        mov       rbx, rcx                          ; path base
        xor       esi, esi                          ; answer, as a byte offset (0 = the whole string)
        mov       rdi, -1                           ; byte offset of this run's first colon
        xor       r12d, r12d                        ; 1 once the run holds a second colon

        vpbroadcastw ymm1, word ptr c_bsl
        vpbroadcastw ymm2, word ptr c_sla
        vpxor     ymm3, ymm3, ymm3

        mov       r9, rcx
        and       r9, -32
        mov       ecx, ebx
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm1
        vpcmpeqw  ymm5, ymm0, ymm2
        vpor      ymm4, ymm4, ymm5
        vpcmpeqw  ymm5, ymm0, ymmword ptr [c_colm]
        vpor      ymm4, ymm4, ymm5
        vpcmpeqw  ymm5, ymm0, ymm3
        vpor      ymm4, ymm4, ymm5
        vpmovmskb eax, ymm4
        shrx      eax, eax, ecx                     ; bit b now means byte b of the path
        neg       ecx
        add       ecx, 32
        mov       r11, rcx                          ; bytes this block covers
        xor       ecx, ecx                          ; byte offset of bit 0 of the mask
        jmp       sp_block

sp_next:
        add       rcx, r11                          ; advance the position base
        mov       r11, 32
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm1
        vpcmpeqw  ymm5, ymm0, ymm2
        vpor      ymm4, ymm4, ymm5
        vpcmpeqw  ymm5, ymm0, ymmword ptr [c_colm]
        vpor      ymm4, ymm4, ymm5
        vpcmpeqw  ymm5, ymm0, ymm3
        vpor      ymm4, ymm4, ymm5
        vpmovmskb eax, ymm4
sp_block:
        test      eax, eax
        jz        sp_next                           ; nothing interesting: skip the whole block

sp_bit:
        tzcnt     edx, eax                          ; byte offset within the mask (even)
        add       rdx, rcx                          ; -> byte offset from the path base
        movzx     r8d, word ptr [rbx + rdx]
        test      r8w, r8w
        jz        sp_end                            ; the terminator: close the run and finish
        cmp       r8w, 003Ah
        jne       sp_slashy

        ; ---- a colon: remember the first, flag a second ----------------------------------------
        cmp       rdi, -1
        jne       sp_dup
        mov       rdi, rdx
        jmp       sp_advance
sp_dup: mov       r12d, 1
        jmp       sp_advance

        ; ---- a backslash or slash: close the run, then apply its own test ----------------------
sp_slashy:
        cmp       rdi, -1
        je        sp_noclose
        test      r12d, r12d
        jnz       sp_noclose                        ; the run had two colons: neither counts
        movzx     r8d, word ptr [rbx + rdi + 2]
        test      r8w, r8w
        jz        sp_noclose
        cmp       r8w, 005Ch
        je        sp_noclose
        cmp       r8w, 002Fh
        je        sp_noclose
        lea       rsi, [rdi + 2]
sp_noclose:
        mov       rdi, -1
        xor       r12d, r12d
        movzx     r8d, word ptr [rbx + rdx + 2]
        test      r8w, r8w
        jz        sp_advance
        cmp       r8w, 005Ch
        je        sp_advance
        cmp       r8w, 002Fh
        je        sp_advance
        lea       rsi, [rdx + 2]

sp_advance:
        blsr      eax, eax                          ; vpcmpeqw sets BOTH bytes of a match, so
        blsr      eax, eax                          ;   clear the pair and move on
        test      eax, eax
        jnz       sp_bit
        jmp       sp_next

sp_end:
        ; the terminator: close the final run the same way
        cmp       rdi, -1
        je        sp_done
        test      r12d, r12d
        jnz       sp_done
        movzx     r8d, word ptr [rbx + rdi + 2]
        test      r8w, r8w
        jz        sp_done
        cmp       r8w, 005Ch
        je        sp_done
        cmp       r8w, 002Fh
        je        sp_done
        lea       rsi, [rdi + 2]
sp_done:
        ; rsi = byte offset of the component, rdx = byte offset of the terminator
        test      rsi, rsi
        jz        sp_ret                            ; already at the front: nothing to move
        mov       r8, rdx
        sub       r8, rsi
        add       r8, 2                             ; bytes to move, terminator included
        lea       r9, [rbx + rsi]                   ; source
        mov       r10, rbx                          ; destination, strictly below the source
sp_move:
        cmp       r8, 32
        jb        sp_m16
        vmovdqu   ymm0, ymmword ptr [r9]            ; load before store: the store lands behind the
        vmovdqu   ymmword ptr [r10], ymm0           ;   next read, so the overlap is harmless
        add       r9, 32
        add       r10, 32
        sub       r8, 32
        jmp       sp_move
sp_m16: cmp       r8, 16
        jb        sp_m8
        vmovdqu   xmm0, xmmword ptr [r9]
        vmovdqu   xmmword ptr [r10], xmm0
        add       r9, 16
        add       r10, 16
        sub       r8, 16
sp_m8:  cmp       r8, 8
        jb        sp_m4
        mov       rax, qword ptr [r9]
        mov       qword ptr [r10], rax
        add       r9, 8
        add       r10, 8
        sub       r8, 8
sp_m4:  cmp       r8, 4
        jb        sp_m2
        mov       eax, dword ptr [r9]
        mov       dword ptr [r10], eax
        add       r9, 4
        add       r10, 4
        sub       r8, 4
sp_m2:  test      r8, r8
        jz        sp_ret
        movzx     eax, word ptr [r9]
        mov       word ptr [r10], ax
sp_ret:
        vzeroupper
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathstrippathw ENDP
END
