; changes/161-pathfindfilenamew/impl.asm
; PWSTR wia_pathfindfilenamew(PCWSTR pszPath)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathFindFileNameW: a pointer to the last component of a path, or to the whole
; string when there is none. 41 ns for a ~90-character path, ~0.45 ns per character.
;
; ---- the rule, and how it was obtained -----------------------------------------------------------
; The separator rule here is genuinely strange, and an earlier attempt at this repository abandoned
; the function after four hypotheses failed. It was not guessed this time, it was DERIVED:
;
;   1. every string over {a, backslash, slash, colon} up to length 8 was enumerated and the live
;      offset recorded -- 87381 observations;
;   2. the scan was modelled as "position i sets the answer to i+1, or it does not", and constraints
;      were collected: for an observed answer A, position A-1 must set and every position at or after
;      A must not. Feeding those into a 3-character window (prev, cur, next) produced exactly FOUR
;      conflicting windows, all of them with cur = ':' -- e.g. the ':' in ":a" sets, the same window
;      in ":a:" must not. So the colon depends on RIGHT context, which is why every local rule
;      failed;
;   3. the conflicts pinned the shape, and the resulting rule was then verified exhaustively.
;
; The rule:
;   * '\' and '/' are always separators. One sets the answer to i+1 when the next character is
;     neither NUL nor '\' nor '/'  (a following ':' is fine).
;   * ':' sets the answer to i+1 under the same next-character test, but only when it is the sole
;     colon in its RUN -- the stretch between two backslash/slash characters. So ":a" gives 1 and
;     "a:a" gives 2, while ":a:" and "a::a" both give 0, and ":\:a" gives 3 because the backslash
;     starts a fresh run in which that colon is alone.
;   * the answer is the last position that set, or the start of the string.
;
; Verified against the live export with ZERO mismatches on 349525 strings over {a, backslash, slash,
; colon} of length 0..9, and on 2396745 strings over {a, backslash, slash, colon, dot, space, z,
; U+4100} of length 0..7.
;
; ---- method --------------------------------------------------------------------------------------
; One forward pass. Per 32-byte block the masks for '\', '/', ':' and NUL are OR-ed into a single
; "interesting positions" mask; a block with none -- the common case inside a long component -- is
; skipped whole, and only the set bits are visited. Run state is two registers: the position of the
; run's first colon, and whether a second one has appeared.
;
; Page-safe: the first load is aligned down to 32 bytes with the leading bytes shifted out of the
; mask, and every later load is 32-aligned. The scan always stops at the terminator because NUL is
; part of the mask.
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
wia_pathfindfilenamew PROC FRAME
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
        jmp       ff_block

ff_next:
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
ff_block:
        test      eax, eax
        jz        ff_next                           ; nothing interesting: skip the whole block

ff_bit:
        tzcnt     edx, eax                          ; byte offset within the mask (even)
        add       rdx, rcx                          ; -> byte offset from the path base
        movzx     r8d, word ptr [rbx + rdx]
        test      r8w, r8w
        jz        ff_end                            ; the terminator: close the run and finish
        cmp       r8w, 003Ah
        jne       ff_slashy

        ; ---- a colon: remember the first, flag a second ----------------------------------------
        cmp       rdi, -1
        jne       ff_dup
        mov       rdi, rdx
        jmp       ff_advance
ff_dup: mov       r12d, 1
        jmp       ff_advance

        ; ---- a backslash or slash: close the run, then apply its own test ----------------------
ff_slashy:
        cmp       rdi, -1
        je        ff_noclose
        test      r12d, r12d
        jnz       ff_noclose                        ; the run had two colons: neither counts
        movzx     r8d, word ptr [rbx + rdi + 2]
        test      r8w, r8w
        jz        ff_noclose
        cmp       r8w, 005Ch
        je        ff_noclose
        cmp       r8w, 002Fh
        je        ff_noclose
        lea       rsi, [rdi + 2]
ff_noclose:
        mov       rdi, -1
        xor       r12d, r12d
        movzx     r8d, word ptr [rbx + rdx + 2]
        test      r8w, r8w
        jz        ff_advance
        cmp       r8w, 005Ch
        je        ff_advance
        cmp       r8w, 002Fh
        je        ff_advance
        lea       rsi, [rdx + 2]

ff_advance:
        blsr      eax, eax                          ; vpcmpeqw sets BOTH bytes of a match, so
        blsr      eax, eax                          ;   clear the pair and move on
        test      eax, eax
        jnz       ff_bit
        jmp       ff_next

ff_end:
        ; the terminator: close the final run the same way
        cmp       rdi, -1
        je        ff_done
        test      r12d, r12d
        jnz       ff_done
        movzx     r8d, word ptr [rbx + rdi + 2]
        test      r8w, r8w
        jz        ff_done
        cmp       r8w, 005Ch
        je        ff_done
        cmp       r8w, 002Fh
        je        ff_done
        lea       rsi, [rdi + 2]
ff_done:
        lea       rax, [rbx + rsi]
        vzeroupper
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathfindfilenamew ENDP
END
