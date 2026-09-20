; changes/219-pathstrippatha/impl.asm
; void wia_pathstrippatha(PSTR pszPath)   [Win64: rcx]
;
; Reimplements shlwapi!PathStripPathA: remove the directory portion of a path IN PLACE, leaving only
; the last component. The live export costs 151.00 ns on a 55-character path against 30.99 ns for
; PathStripPathW on the SAME path, 4.87x the wide cost for HALF the bytes, the MBCS-walk signature
; the whole narrow shlwapi family has shown.
;
; This is change 212 Plus a move, and the equivalence was verified rather than assumed, on both
; halves at once (probes/strip.c): over every string in {a, backslash, slash, colon} up to length 8
; 87381 of them, and again over {a, backslash, slash, colon, SPACE} up to length 8, 488281, of
; which 400900 contain a space; the buffer left by the live PathStripPathA is byte for byte what
; you get by copying the live PathFindFileNameA answer to the front. ZERO mismatches, and zero for
; PathStripPathW against the same model.
;
; The space was checked deliberately. Change 132 shipped a PathFindExtension rule missing exactly
; that character and was wrong on 295513 of 2015539 strings, and changes 140, 143 and 144 inherited
; it. PathFindFileName's rule came through the same widening clean, and so does this one, but it
; was measured, for the narrow form AND for the landed wide one.
;
; ---- the first question was not "is it slow" --------------------------------------------------------
; It was whether the observable behaviour is BYTE-WISE on this machine. An MBCS walk that treated some
; byte as a lead byte would step over the byte after it, and a separator hiding there would be
; invisible to the export and visible to a byte scan. probes/pffa.c tried every byte value 0x01..0xFF
; in exactly that position: ZERO of 255 behave as a lead byte (GetACP() is 1252, which has none). A
; vector scan can reproduce this exactly.
;
; The live one also leaves the bytes PAST the new terminator untouched, stripping "C:\dir\file.txt"
; leaves "file.txt\0" followed by the stale tail "le.txt\0", so the move is a plain forward copy
; with no zero fill, and the correctness harness compares the whole buffer to prove it.
;
; The copy is forward and the destination is strictly below the source, so overlap is safe as long as
; each block is loaded before it is stored: the store then lands entirely behind the next block's
; read. The usual head/tail overlapping-pair trick would NOT be safe here, and change 218 proved that
; the hard way; it borrowed the pair from change 211, whose source and destination are different
; buffers, and re-read bytes it had already moved. The remainder below therefore walks DOWN
; 16/8/4/2/1 instead of overlapping.
;
; ---- the rule, and why it was re-derived rather than inherited --------------------------------------
; Change 161 did not guess the WIDE rule either, an earlier attempt abandoned the function after four
; hypotheses failed, because the colon depends on RIGHT context. The narrow form gets the same
; treatment, because this project keeps getting punished for assuming an A form matches its W: change
; 203 inherited 202's contract exactly, change 205's REJECTED the braces ntdll's parser requires, and
; lstrcmpA turned out to be linguistic where the name suggested otherwise.
;
; probes/rule.c enumerated every string over {a, backslash, slash, colon} of length 0..9, 349525 of
; them, and compared the live NARROW export against two models:
;
;     mismatches vs the 161 (wide) rule : 0
;     mismatches vs a simpler rule      : 76672
;
; plus 2396745 strings over {a, backslash, slash, colon, dot, space, z, 0xE9}: 0 mismatches. So the
; narrow form carries the wide rule exactly, run condition included, and the simpler rule that every
; spot check in probes/pffa.c was consistent with is wrong on 76672 strings. Spot checks would never
; have found it; only the enumeration did.
;
; The rule:
;   * '\' and '/' are always separators. One sets the answer to i+1 when the next character is
;     neither NUL nor '\' nor '/'  (a following ':' is fine).
;   * ':' sets the answer to i+1 under the same next-character test, but only when it is the sole
;     colon in its RUN; the stretch between two backslash/slash characters. So ":a" gives 1 and
;     "a:a" gives 2, while ":a:" and "a::a" both give 0, and ":\:a" gives 3 because the backslash
;     starts a fresh run in which that colon is alone.
;   * the answer is the last position that set, or the start of the string.
;
; ---- method ----------------------------------------------------------------------------------------
; One forward pass finds the component, then a forward move brings it to the front.
; Per 32-byte block the masks for '\', '/', ':' and NUL are OR-ed into a single
; "interesting positions" mask; a block with none (the common case inside a long component) is
; skipped whole, and only the set bits are visited. Run state is two registers: the position of the
; run's first colon, and whether a second one has appeared.
;
; A narrow block carries 32 positions where the wide form's carried 16, and each match sets ONE mask
; bit rather than a pair, so the per-bit step is a single blsr instead of two.
;
; Page-safe: the first load is aligned down to 32 bytes with the leading bytes shifted out of the
; mask, and every later load is 32-aligned, so no load ever touches a page the byte-at-a-time export
; would not have reached. The scan always stops at the terminator because NUL is part of the mask.
; The one-past reads ([pos+1], the next-character test) are only issued when the character AT pos is
; not NUL, so the byte they touch is at worst the terminator itself, always mapped.
;
; ISA: AVX2 + BMI1 (tzcnt, blsr) + BMI2 (shrx). Validated on Zen 4.
;
; Only ymm0-ymm5 are used. xmm6-xmm15 are callee-saved under Win64 (their low 128 bits are) and
; parking a constant in ymm6, as an earlier cut of change 161 did, silently destroys any double the
; caller had live. Invisible to a correctness test, which compares pointers and characters. See
; tools/abi-check. ':' is the rarest of the four and is only compared against, so it becomes a memory
; operand and its register disappears; the other three stay in registers.
.const
ALIGN 16
c_bsl   db 05Ch
c_sla   db 02Fh
; 32-byte form, for use as a memory operand. VEX operands need no alignment, so no ALIGN 32
; (which .const rejects with A2189).
c_colm  db 32 dup(03Ah)

.code
wia_pathstrippatha PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        .endprolog

        test      rcx, rcx
        jz        ff_ret                            ; measured: NULL returns without faulting

        mov       rbx, rcx                          ; path base
        xor       esi, esi                          ; answer, as a byte offset (0 = the whole string)
        mov       rdi, -1                           ; byte offset of this run's first colon
        xor       r12d, r12d                        ; 1 once the run holds a second colon

        vpbroadcastb ymm1, byte ptr c_bsl
        vpbroadcastb ymm2, byte ptr c_sla
        vpxor     ymm3, ymm3, ymm3

        mov       r9, rcx
        and       r9, -32                           ; align DOWN: never touches an earlier page
        mov       ecx, ebx
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm1
        vpcmpeqb  ymm5, ymm0, ymm2
        vpor      ymm4, ymm4, ymm5
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_colm]
        vpor      ymm4, ymm4, ymm5
        vpcmpeqb  ymm5, ymm0, ymm3
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
        vpcmpeqb  ymm4, ymm0, ymm1
        vpcmpeqb  ymm5, ymm0, ymm2
        vpor      ymm4, ymm4, ymm5
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_colm]
        vpor      ymm4, ymm4, ymm5
        vpcmpeqb  ymm5, ymm0, ymm3
        vpor      ymm4, ymm4, ymm5
        vpmovmskb eax, ymm4
ff_block:
        test      eax, eax
        jz        ff_next                           ; nothing interesting: skip the whole block

ff_bit:
        tzcnt     edx, eax                          ; byte offset within the mask
        add       rdx, rcx                          ; -> byte offset from the path base
        movzx     r8d, byte ptr [rbx + rdx]
        test      r8b, r8b
        jz        ff_end                            ; the terminator: close the run and finish
        cmp       r8b, 03Ah
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
        movzx     r8d, byte ptr [rbx + rdi + 1]
        test      r8b, r8b
        jz        ff_noclose
        cmp       r8b, 05Ch
        je        ff_noclose
        cmp       r8b, 02Fh
        je        ff_noclose
        lea       rsi, [rdi + 1]
ff_noclose:
        mov       rdi, -1
        xor       r12d, r12d
        movzx     r8d, byte ptr [rbx + rdx + 1]
        test      r8b, r8b
        jz        ff_advance
        cmp       r8b, 05Ch
        je        ff_advance
        cmp       r8b, 02Fh
        je        ff_advance
        lea       rsi, [rdx + 1]

ff_advance:
        blsr      eax, eax                          ; one bit per match here, unlike the wide form
        test      eax, eax
        jnz       ff_bit
        jmp       ff_next

ff_end:
        ; the terminator: close the final run the same way
        cmp       rdi, -1
        je        ff_done
        test      r12d, r12d
        jnz       ff_done
        movzx     r8d, byte ptr [rbx + rdi + 1]
        test      r8b, r8b
        jz        ff_done
        cmp       r8b, 05Ch
        je        ff_done
        cmp       r8b, 02Fh
        je        ff_done
        lea       rsi, [rdi + 1]
ff_done:
        ; rsi = byte offset of the last component, rdx = byte offset of the terminator.
        ; ff_done is reached only through ff_end, which is the branch taken when the scan meets the
        ; terminator, so rdx is the string's length here.
        test      rsi, rsi
        jz        ff_ret                            ; already at the front: nothing to move
        mov       r8, rdx
        sub       r8, rsi
        inc       r8                                ; bytes to move, terminator included
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
sp_m2:  cmp       r8, 2
        jb        sp_m1
        movzx     eax, word ptr [r9]
        mov       word ptr [r10], ax
        add       r9, 2
        add       r10, 2
        sub       r8, 2
sp_m1:  test      r8, r8
        jz        ff_ret
        movzx     eax, byte ptr [r9]                ; a narrow move can end on an ODD byte, which the
        mov       byte ptr [r10], al                ;   wide form never had to handle
ff_ret:
        vzeroupper
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathstrippatha ENDP
END
