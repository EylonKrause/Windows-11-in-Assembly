; changes/288-foldstringw-digits/impl.asm
;   int wia_foldstringw_digits(DWORD flags, PCWSTR src, int cchSrc, PWSTR dest, int cchDest)
;                                            [Win64: ecx, rdx, r8d, r9, [rsp+40] -> eax]
;
; kernelbase!FoldStringW, the MAP_FOLDDIGITS path.
;
; --------------------------------------------------------------------------------------------------
; 1. THE NUMBER. discovery/uncovered_2026b.c measured MAP_FOLDDIGITS at 636.57 ns for 511 code units,
; 0.623 ns per byte -- the second most expensive uncovered export in that sweep, behind only lstrcmpiW,
; which is a known collation wall.
;
; --------------------------------------------------------------------------------------------------
; 2. This change implements one of five flag paths, and the scope was measured rather than chosen.
;
; FoldStringW is five functions behind one entry point. probes/contract.c folded every code unit on its
; own, one flag at a time, and counted what came back:
;
;      flag                   grew   max units out   1-unit failures   changed 1:1
;      MAP_FOLDDIGITS            0         1                0             462
;      MAP_FOLDCZONE          1169        18             2082            1798
;      MAP_PRECOMPOSED          79         3             2082             486
;      MAP_COMPOSITE         12197         4             2082             468
;      MAP_EXPAND_LIGATURES    710         3                0               0
;
; MAP_FOLDDIGITS is the only strictly 1:1 flag. Every other one turns a single input unit into several --
; up to EIGHTEEN for one MAP_FOLDCZONE input -- and a mapping that changes the length is not a
; per-character table at any width. Composition additionally depends on neighbouring characters, which
; is the same wall changes 274 and 276 died on.
;
; So those four flags are separate problems, and this function DECLINES them with ERROR_INVALID_FLAGS
; rather than pretending. The repository has the precedent: crypt32!CryptBinaryToStringA is covered by
; four separate changes, one per output format, and materialize.py writes all their bodies.
;
; For MAP_FOLDDIGITS the two enabling questions came back clean: CONTEXT-FREEDOM (20000 random strings
; up to 2048 units, 0 words differing from the per-character result and no length ever changing) and
; LOCALE INVARIANCE (the whole table rebuilt under seven thread locales, 0 entries different).
;
; --------------------------------------------------------------------------------------------------
; 3. The rest of the contract, all measured.
;
;   * cchSrc > 0 is a count; cchSrc == -1 is NUL-terminated and includes the terminator, so "abc" gives 4;
;   * cchDest == 0 is a LENGTH QUERY: return the required count and write nothing;
;   * cchDest too small returns 0 with ERROR_INSUFFICIENT_BUFFER and writes nothing -- measured: the
;     destination's first word was still its sentinel afterwards;
;   * cchSrc == 0 returns 0 with ERROR_INVALID_PARAMETER. probes/contract.c first reported
;     ERROR_INSUFFICIENT_BUFFER, and that was a measurement bug rather than a fact: it read GetLastError
;     without resetting it, so it reported the 122 left by the preceding too-small-buffer call. The
;     correctness gate resets before every call and got 87;
;   * a NULL source returns 0 with ERROR_INVALID_PARAMETER;
;   * dest == src is refused with ERROR_INVALID_PARAMETER, and the check is pointer equality only:
;     dest = src+1, src+4, src+8 and src-4 all SUCCEED. The documentation calls overlap illegal; the
;     export only rejects the exact-equality case, and a replacement has to match the export.
;     probes/overlap.c then measured WHAT it produces for the accepted overlaps: exactly what a naive
;     forward one-unit-at-a-time loop produces, at every offset. It reads units it has already
;     overwritten rather than buffering, which is unspecified by the documentation and entirely
;     deterministic in fact -- so this implementation drops its unroll when the buffers overlap;

;   * a NULL destination is refused only when cchDest is non-zero, and the whole refusal order is
;     OBSERVABLE. probes/nulldest.c exists because of a mutation survivor: mutant #10 deleted the
;     NULL-destination refusal and the correctness gate still passed 66,410 cases with 0 mismatches,
;     because not one of them passed a NULL destination together with a non-zero cchDest. The corpus
;     could not express the case -- and worse, the refusal had never been MEASURED at all. It was
;     written from the natural assumption that a NULL destination must be refused, and reference.c
;     inherited the same assumption from this file rather than from the export, so the three-way
;     comparison was blind to it: an assumption shared by both sides of a comparison cannot be caught
;     by comparing them.
;
;     Measured, the order is:
;
;         1.  src == NULL                        -> 0 / ERROR_INVALID_PARAMETER
;         2.  dest == src                        -> 0 / ERROR_INVALID_PARAMETER
;         3.  cchSrc == 0                        -> 0 / ERROR_INVALID_PARAMETER
;         4.  cchDest != 0 and dest == NULL      -> 0 / ERROR_INVALID_PARAMETER
;         5.  the flag is not supported          -> 0 / ERROR_INVALID_FLAGS
;         6.  cchDest == 0                       -> the required count, nothing written
;         7.  cchDest < the required count       -> 0 / ERROR_INSUFFICIENT_BUFFER
;
;     Every step of that order is observable and was observed:
;
;       - dest NULL with cchDest 0 SUCCEEDS and returns the count (3 for three units, 4 for cchSrc -1
;         on a three-unit string), so step 4 is conditional on cchDest and cannot simply reject NULL;
;       - dest NULL with cchDest 2 against a 3-unit string gives 87, NOT 122, so step 4 runs BEFORE
;         step 7. This file had it the other way round and was wrong: the NULL test sat after the
;         buffer test, so that input returned ERROR_INSUFFICIENT_BUFFER. A real defect, shipped in the
;         draft, found only because a mutant survived;
;       - an unsupported flag with valid pointers and cchDest 0 gives 1004, but the SAME flag with a
;         NULL destination and cchDest 64 gives 87, so step 5 runs after steps 1-4 and before 6-7.
;         That is why the flag test below sits where it does rather than first: placed first it would
;         report 1004 for inputs on which the export reports 87. Putting it fifth makes this function
;         agree with the export on every refusal INCLUDING the flags it declines, which narrows the
;         declared divergence to exactly one thing: a supported-but-unimplemented flag with wholly
;         valid parameters, where the export folds and this function returns 0 / 1004;
;       - a five-unit unterminated string ending exactly at a guard page, with cchSrc -1 and a NULL
;         destination, RETURNS 87 rather than faulting, so the export refuses before it scans. Steps
;         1-5 therefore all precede the length scan here too, and a caller can hand us precisely that.
;
; The last-error value is written straight to the TEB at gs:[68h] rather than through SetLastError,
; which keeps this a leaf with no imports. That offset is stable on x64 but it is not guessed at here:
; the correctness gate compares GetLastError after our call against the live export's on every refusal,
; so a wrong offset fails the gate rather than passing silently.
;
; --------------------------------------------------------------------------------------------------
; 4. THE ALGORITHM. A flat 65536-entry table, one load and one store per unit, unrolled by eight. 462
; entries differ from the identity and 65073 do not, which is exactly why the table is flat rather than
; sparse: change 287 measured a two-level layout for the same shape at 2.25x against 3.79x flat, because
; the saving cost a compare, a branch and a second dependent load per unit.
;
; ISA: AVX2 for the length scan only.
; --------------------------------------------------------------------------------------------------

OPTION PROC:PRIVATE

EXTERN wia_fold_digit:WORD

MAP_FOLDDIGITS_    EQU 0080h
E_INVALID_FLAGS    EQU 1004
E_INVALID_PARAM    EQU 87
E_INSUFF_BUFFER    EQU 122
TEB_LASTERROR      EQU 68h

                .code

; ---------------------------------------------------------------------------------------------
; foldlen -- the address of the terminator of the string at rcx, in rax.
; Clobbers rax, rcx, rdx, r8, ymm0, ymm5.
; ---------------------------------------------------------------------------------------------
foldlen PROC PRIVATE
        mov       r8, rcx
        vpxor     ymm5, ymm5, ymm5
        mov       rdx, rcx
        and       r8, -32
        sub       rdx, r8
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm0, ymm0, ymm5
        vpmovmskb eax, ymm0
        mov       ecx, edx
        mov       edx, -1
        shl       edx, cl
        and       eax, edx
        test      eax, eax
        jnz       fl_hit
fl_loop:
        add       r8, 32
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm0, ymm0, ymm5
        vpmovmskb eax, ymm0
        test      eax, eax
        jz        fl_loop
fl_hit:
        tzcnt     eax, eax
        and       eax, -2
        add       rax, r8
        ret
foldlen ENDP

                PUBLIC wia_foldstringw_digits

wia_foldstringw_digits PROC FRAME
        push      r15
        .pushreg  r15
        push      r14
        .pushreg  r14
        push      r13
        .pushreg  r13
        push      r12
        .pushreg  r12
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        .endprolog
        ; the fifth argument sits above the shadow space: [rsp+40] at entry, and the seven pushes move
        ; it to [rsp+96].
        mov       r14d, dword ptr [rsp + 96]      ; cchDest

        ; ---- The refusals, in the order probes/nulldest.c measured (see section 3). The order is
        ; observable, this file had two steps of it wrong, and the flag test is FIFTH on purpose.
        test      rdx, rdx
        jz        f_badparam                      ; 1. a NULL source
        cmp       rdx, r9
        je        f_badparam                      ; 2. dest == src exactly; partial overlap is allowed
        test      r8d, r8d
        jz        f_badparam                      ; 3. cchSrc == 0 gives ERROR_INVALID_PARAMETER
        test      r14d, r14d
        jz        f_dest_ok                       ; 4. cchDest == 0 needs no destination at all...
        test      r9, r9
        jz        f_badparam                      ;    ...but any other cchDest does, and this comes
f_dest_ok:                                        ;       BEFORE the buffer test, not after it
        cmp       ecx, MAP_FOLDDIGITS_
        jne       f_badflags                      ; 5. one of five paths; the other four are separate
                                                  ;    problems, and this sits after 1-4 so that a
                                                  ;    declined flag with a bad pointer still reports
                                                  ;    the pointer error the export reports

        mov       rsi, rdx                        ; the source
        mov       rdi, r9                         ; the destination

        ; ---- the count. cchSrc < 0 is NUL-terminated and includes the terminator.
        mov       r12d, r8d
        test      r12d, r12d
        jns       f_have_count
        mov       rcx, rsi
        call      foldlen
        sub       rax, rsi
        sar       rax, 1
        inc       rax
        mov       r12d, eax
        vzeroupper
f_have_count:

        ; ---- cchDest == 0 is a length query: return the count and write nothing
        test      r14d, r14d
        jz        f_query
        cmp       r14d, r12d
        jl        f_smallbuf                      ; too small: return 0 and write NOTHING
                                                  ; (the destination was already checked at step 4:
                                                  ;  the export refuses a NULL one BEFORE it looks at
                                                  ;  the size, which is the defect this file had)

        lea       r11, wia_fold_digit

        xor       r13, r13
        mov       eax, r12d
        and       eax, 7
        mov       r15d, eax
        mov       eax, r12d
        sub       eax, r15d
        mov       r8d, eax

        ; ---- Overlapping buffers drop the unroll, and that is a measured requirement.
        ;
        ; dest == src is refused above, but every other overlap is ACCEPTED by the export, and
        ; probes/overlap.c measured what it then produces: exactly what a naive forward
        ; one-unit-at-a-time loop produces, at every offset -- it reads units it has already
        ; overwritten rather than buffering. With twelve Arabic-Indic digits and dest = src+3 the
        ; export returns 0030 0031 0032 repeated, which is the signature of reading its own output.
        ;
        ; So the behaviour is unspecified by the documentation but perfectly deterministic, and the
        ; unrolled loop below does NOT reproduce it: reading eight units before writing them gives a
        ; different answer for small offsets, which the correctness gate caught at dest = src+1, +2
        ; and +3. Overlapping inputs therefore take the one-at-a-time tail loop, which is the naive
        ; loop by construction. The cost falls only on inputs the documentation already forbids.
        mov       rax, r12
        shl       rax, 1                          ; the length in bytes
        mov       rcx, rsi
        add       rcx, rax                        ; one past the source
        mov       rdx, rdi
        add       rdx, rax                        ; one past the destination
        cmp       rdi, rcx
        jae       f_no_overlap
        cmp       rsi, rdx
        jae       f_no_overlap
        xor       r8d, r8d                        ; overlapping: everything through the tail loop
        mov       r15d, r12d
f_no_overlap:

        test      r8d, r8d
        jz        f_tail

f_loop:
        movzx     eax, word ptr [rsi + r13*2]
        movzx     ecx, word ptr [rsi + r13*2 + 2]
        movzx     edx, word ptr [rsi + r13*2 + 4]
        movzx     r9d, word ptr [rsi + r13*2 + 6]
        movzx     eax, word ptr [r11 + rax*2]
        movzx     ecx, word ptr [r11 + rcx*2]
        movzx     edx, word ptr [r11 + rdx*2]
        movzx     r9d, word ptr [r11 + r9*2]
        mov       word ptr [rdi + r13*2], ax
        mov       word ptr [rdi + r13*2 + 2], cx
        mov       word ptr [rdi + r13*2 + 4], dx
        mov       word ptr [rdi + r13*2 + 6], r9w

        movzx     eax, word ptr [rsi + r13*2 + 8]
        movzx     ecx, word ptr [rsi + r13*2 + 10]
        movzx     edx, word ptr [rsi + r13*2 + 12]
        movzx     r9d, word ptr [rsi + r13*2 + 14]
        movzx     eax, word ptr [r11 + rax*2]
        movzx     ecx, word ptr [r11 + rcx*2]
        movzx     edx, word ptr [r11 + rdx*2]
        movzx     r9d, word ptr [r11 + r9*2]
        mov       word ptr [rdi + r13*2 + 8], ax
        mov       word ptr [rdi + r13*2 + 10], cx
        mov       word ptr [rdi + r13*2 + 12], dx
        mov       word ptr [rdi + r13*2 + 14], r9w

        add       r13, 8
        sub       r8d, 8
        jnz       f_loop

f_tail:
        test      r15d, r15d
        jz        f_ok
f_tloop:
        movzx     eax, word ptr [rsi + r13*2]
        movzx     eax, word ptr [r11 + rax*2]
        mov       word ptr [rdi + r13*2], ax
        inc       r13
        dec       r15d
        jnz       f_tloop

f_ok:
        mov       eax, r12d
        jmp       f_ret
f_query:
        mov       eax, r12d
        jmp       f_ret

f_badflags:
        mov       dword ptr gs:[TEB_LASTERROR], E_INVALID_FLAGS
        xor       eax, eax
        jmp       f_ret
f_badparam:
        mov       dword ptr gs:[TEB_LASTERROR], E_INVALID_PARAM
        xor       eax, eax
        jmp       f_ret
f_smallbuf:
        mov       dword ptr gs:[TEB_LASTERROR], E_INSUFF_BUFFER
        xor       eax, eax

f_ret:
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        pop       r12
        pop       r13
        pop       r14
        pop       r15
        ret
wia_foldstringw_digits ENDP

                END
