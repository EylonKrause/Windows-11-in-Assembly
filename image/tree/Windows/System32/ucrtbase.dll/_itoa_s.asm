; ucrtbase.dll!_itoa_s  --  hand-written x86-64 reimplementation (1.73x vs shipped)
; source of truth: changes/198-itoa-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/198-itoa-s/impl.asm
; errno_t wia_itoa_s(int Value, char* Buffer, size_t SizeInChars, int Radix)
;   [rcx, rdx, r8, r9d -> eax]
;
; Reimplements ucrtbase!_itoa_s. `_ltoa_s` is a SEPARATE export at a different address (0x00035780
; against 0x000357B0) that the compiler laid out with the branch inverted, but it is the same
; function: same shared worker, same arguments, same behaviour. One implementation covers both
; names, and the harness checks both exports rather than assuming it.
;
; Bounded form of change 056.
;
; THE 32-BIT FAMILY IS THE SAME MACHINE, HALF THE WIDTH. ucrtbase lays it out identically to the
; 64-bit one: the signed entry computes `negative = (Radix == 10 && Value < 0)` and calls a shared
; worker (0x0003588C, mirroring 0x00079DAC), the unsigned entry passes a hard zero to the same
; address, and the worker tail-jumps to a digit emitter (0x000654D0, mirroring 0x00076F10) that
; emits least-significant-first and reverses in place. Instruction for instruction the two workers
; are the same code with `mov r10d, ecx` where the 64-bit one has `mov r10, rcx`, and `div eax, edi`
; where it has `div rax, rdi`.
;
; THAT ONE DIFFERENCE IS THE WHOLE CONTRACT DIFFERENCE: the magnitude is 32 bits, so for any radix
; other than 10 the value is formatted as an UNSIGNED 32-BIT quantity. _itoa_s(-1, buf, n, 16)
; gives "ffffffff" -- eight f's, not the sixteen that change 194 produces.
;
; Everything else is change 194's contract, read out of the shipped disassembly because the ERANGE
; path could not be fitted from probing:
;   * Buffer == NULL or SizeInChars == 0 -> EINVAL (22), NOTHING written;
;   * otherwise Buffer[0] = 0 is written IMMEDIATELY, before the rest of the validation;
;   * SizeInChars <= negative + 1 -> ERANGE (34) before a single digit is emitted;
;   * Radix outside 2..36 -> EINVAL (22), Buffer[0] = 0;
;   * a buffer that runs out keeps exactly the REVERSED prefix it held, then Buffer[0] = 0;
;   * errno is set BEFORE the invalid-parameter handler on both error paths.
;
; Method is change 194's: digits into a stack scratch FIRST so the fit is decided before the
; caller's buffer is touched, then copied forward (success) or BACKWARD (the ERANGE partial).
; Radix 10 uses the assembled 2-digit table, every power-of-two radix shifts, the rest divide --
; and here every division is a 32-bit `div`, which is several times cheaper than the 64-bit one.
;
; ISA: baseline x64.

EXTERN _errno:PROC
EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
; Maximum digits a 32-bit unsigned value needs, indexed by radix. One compare against
; this decides, BEFORE the emit begins, whether the buffer could possibly be too small;
; if it cannot be, the fast loops below run with no bound check at all.
maxdig  db 0,0,32,21,16,14,13,12,11,11,10,10,9,9,9,9,8,8,8,8,8,8,8,8,7,7,7,7,7,7,7,7,7,7,7,7,7


dec2b   db "00010203040506070809"
        db "10111213141516171819"
        db "20212223242526272829"
        db "30313233343536373839"
        db "40414243444546474849"
        db "50515253545556575859"
        db "60616263646566676869"
        db "70717273747576777879"
        db "80818283848586878889"
        db "90919293949596979899"

.code
wia_itoa_s PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        sub       rsp, 68h                     ; 32 shadow + 72 scratch; rsp ends 16-aligned
        mov       rbx, rdx                     ; Buffer
        mov       rsi, r8                      ; SizeInChars
        mov       r10d, r9d                    ; Radix

        test      rbx, rbx
        jz        e_inval_none
        test      rsi, rsi
        jz        e_inval_none                 ; nothing is written in either case

        mov       byte ptr [rbx], 0            ; written BEFORE the remaining validation

        xor       r12d, r12d                   ; negative
        cmp       r10d, 10
        jne       nn
        test      ecx, ecx
        jns       nn
        mov       r12d, 1
        neg       ecx                          ; magnitude; INT_MIN negates to 2^31 unsigned
nn:
        lea       rax, [r12 + 1]
        cmp       rsi, rax
        jbe       e_range                      ; size <= negative+1 -> ERANGE before any digit
        mov       eax, r10d
        sub       eax, 2
        cmp       eax, 34
        ja        e_inval_wrote                ; radix outside 2..36

        ;================ emit the digits FORWARD into the scratch ================
        ; CAN THE BUFFER EVEN BE TOO SMALL? Decide here, once, instead of bound-checking every
        ; digit. ucrtbase emits straight into the caller's buffer and stops when full; generating
        ; every digit and only then finding it does not fit made the 10-digit-into-6-cells case
        ; measure 0.93x -- a regression -- because a 32-bit `div` is cheap enough for the extra
        ; digits to show. But putting the bound check INSIDE the loops cost ~1.5 cycles per digit
        ; and was worse still: base 36 fell to 0.88x and base 2 from 3.98x to 2.51x.
        ; So the check is hoisted. If the cells available cannot hold the widest possible result
        ; for this radix, the bounded loop below runs; otherwise the fast loops run untouched.
        mov       r11, rsi
        sub       r11, r12                     ; cells available for digits = size - negative
        lea       rax, maxdig
        movzx     eax, byte ptr [rax + r10]    ; widest result this radix can produce
        lea       rdi, [rsp + 68h]              ; one past the scratch; stores pre-decrement
        cmp       r11, rax
        ja        fits_for_sure                ; cells > widest result => digits + terminator fit
        ; ---- bounded emit: only reachable when the buffer really might be too small, so it is
        ; ---- kept simple -- one generic division per digit, every radix, no table, no shift.
        mov       rax, rdi
        sub       rax, r11
        mov       r11, rax                     ; r11 = the lowest address the digits may reach
        mov       eax, ecx                     ; magnitude
        mov       ecx, r10d
        lea       r8, [rbx + r12]              ; ...and a FORWARD cursor into the caller's buffer.
        ; Each digit is stored TWICE: descending into the scratch (so the fitted case can copy it
        ; out most-significant-first) and ascending into the caller's buffer. The second store is
        ; free -- this loop is bound by a 32-bit `div` at ~15 cycles, which leaves the store ports
        ; idle -- and it is exactly the byte ucrtbase would have left there, because ucrtbase emits
        ; into the caller's buffer least-significant-first and simply stops when it runs out. So
        ; the ERANGE exit below has nothing left to do but write the terminator.
        ; This is what took the "10 digits b10 ERANGE" class from 0.93x to a win: the old code
        ; generated into the scratch and then walked it back out a byte at a time, and that second
        ; dependent loop was the entire deficit against ucrtbase.
        ; RADIX 10 GETS ITS OWN BOUNDED LOOP. This path is the ERANGE case, and ERANGE is
        ; dominated by two calls into ucrtbase (_errno and _invalid_parameter_noinfo) that our
        ; contract obliges us to make and that ucrtbase pays too -- so the only part of the class
        ; we can actually win is the digit loop, and at the generic `div` it was an exact tie.
        ; Radix 10 is a compile-time constant here, so the divide becomes a constant reciprocal:
        ; v/10 == (v * 0CCCCCCCDh) >> 35 for EVERY 32-bit v (verified exhaustively near both ends
        ; of the range and over 500k random values). imul+shr is a ~4-cycle loop-carried chain
        ; against the divider's ~10-12.
        cmp       r10d, 10
        jne       ce_l
        mov       r9d, 0CCCCCCCDh              ; zero-extends; the immediate form would sign-extend
c10_l:
        mov       edx, eax
        imul      rdx, r9                      ; v * M   (< 2^64: 0FFFFFFFFh * 0CCCCCCCDh = 1.48e19)
        shr       rdx, 35                       ; q = v / 10
        lea       ecx, [rdx + rdx*4]
        add       ecx, ecx                      ; q * 10
        sub       eax, ecx                      ; rem = v - q*10, 0..9
        add       eax, 30h                      ; ...which for radix 10 is always a plain digit
        dec       rdi
        mov       byte ptr [rdi], al
        mov       byte ptr [r8], al
        inc       r8
        mov       eax, edx                      ; v = q
        test      eax, eax
        jz        emitted                       ; it fitted after all
        cmp       rdi, r11
        ja        c10_l
        jmp       capped

ce_l:
        xor       edx, edx
        div       ecx
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        ce_s
        lea       r9d, [rdx + 57h]
ce_s:
        dec       rdi
        mov       byte ptr [rdi], r9b
        mov       byte ptr [r8], r9b           ; the reversed prefix, written as it is generated
        inc       r8
        test      eax, eax
        jz        emitted                      ; it fitted after all
        cmp       rdi, r11
        ja        ce_l
        jmp       capped

fits_for_sure:
        lea       rdi, [rsp + 68h]             ; one past the scratch; every store pre-decrements
        mov       eax, ecx                     ; magnitude; the 32-bit mov zero-extends, which
                                               ; is exactly the unsigned 32-bit value a radix
                                               ; other than 10 must format
        cmp       r10d, 10
        je        d10
        ; Every power-of-two radix -- 2, 4, 8, 16, 32 -- can shift instead of divide. Only radix 16
        ; was special-cased at first, which left radix 2 issuing 64 divisions and measuring a 1.00x
        ; tie; with the shift it becomes one of the widest wins here. The test is done in r9d, not
        ; eax, because eax already holds the magnitude.
        lea       r9d, [r10 - 1]
        test      r9d, r10d
        jnz       dgen
        mov       r11d, r9d                    ; mask = radix-1
        bsf       ecx, r10d                    ; shift = log2(radix)
        jmp       dpow

dgen:                                          ; the rare radixes keep a division, like ucrtbase
        ; MEASURED, NOT ASSUMED: a reciprocal multiply was tried here and is NOT faster on this
        ; core. Two forms were built and benchmarked against this divide, both bit-exact:
        ;   * magic scaled to 2^38, quotient extracted with `shrd rax, rdx, 38` -- base 36 went
        ;     8.19 ns -> 10.45 ns (0.99x -> 0.77x); shrd-with-immediate is multi-uop here;
        ;   * magic scaled to 2^64, quotient arriving in rdx with no shift at all -- 8.45 ns, still
        ;     short of the divide.
        ; Zen 4's 32-bit divider is simply fast enough that a dependent `mul` chain does not beat
        ; it at these digit counts, and the divide keeps the loop four instructions shorter.
        mov       ecx, r10d
dgen_l:
        xor       edx, edx
        div       ecx
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        dgen_s
        lea       r9d, [rdx + 57h]             ; lowercase for values above 9
dgen_s:
        dec       rdi
        mov       byte ptr [rdi], r9b
        test      eax, eax
        jnz       dgen_l
        jmp       emitted

d10:
        lea       r8, dec2b
        mov       ecx, 100
d10_l:
        cmp       eax, 100
        jb        d10_last
        xor       edx, edx
        div       ecx
        sub       rdi, 2
        mov       r9w, word ptr [r8 + rdx*2]   ; two decimal digits at a time
        mov       word ptr [rdi], r9w
        jmp       d10_l
d10_last:
        cmp       eax, 10
        jb        d10_one
        sub       rdi, 2
        mov       r9w, word ptr [r8 + rax*2]
        mov       word ptr [rdi], r9w
        jmp       emitted
d10_one:
        dec       rdi
        add       eax, 30h
        mov       byte ptr [rdi], al
        jmp       emitted

dpow:                                          ; radix is a power of two: shift, never divide
        mov       edx, eax
        and       edx, r11d
        lea       r9d, [rdx + 30h]
        cmp       edx, 10
        jb        dpow_s
        lea       r9d, [rdx + 57h]
dpow_s:
        dec       rdi
        mov       byte ptr [rdi], r9b
        shr       eax, cl
        jnz       dpow

emitted:
        lea       r8, [rsp + 68h]              ; one past the last (least significant) digit
        mov       rcx, r8
        sub       rcx, rdi                     ; rcx = digit count

        ;================ does it fit? negative + digits + terminator ================
        lea       rax, [r12 + rcx + 1]
        cmp       rax, rsi
        ja        partial

        mov       rdx, rbx
        test      r12d, r12d
        jz        s_nosign
        mov       byte ptr [rdx], 2Dh          ; '-'
        inc       rdx
s_nosign:
        ; Copy the digits 8 bytes at a time. A byte-at-a-time loop cost ~13 cycles on a 13-digit
        ; base-36 value and put that class at 0.92x -- a regression. Every wide store here is
        ; provably inside the buffer: success means negative + digits + 1 <= SizeInChars, so while
        ; 8 or more digits remain there are at least 9 cells left.
        mov       rax, rcx                     ; digit count
        cmp       rax, 8
        jb        s_small
s_l8:
        mov       r9, qword ptr [rdi]
        mov       qword ptr [rdx], r9
        add       rdi, 8
        add       rdx, 8
        sub       rax, 8
        cmp       rax, 8
        jae       s_l8
        test      rax, rax
        jz        s_done
        lea       rdi, [rdi + rax - 8]         ; overlapping final 8, still inside the digit run
        lea       rdx, [rdx + rax - 8]
        mov       r9, qword ptr [rdi]
        mov       qword ptr [rdx], r9
        lea       rdx, [rdx + 8]
        jmp       s_done
s_small:
        test      rax, rax
        jz        s_done
        ; 4..7 digits in TWO overlapping 4-byte moves instead of up to seven dependent byte
        ; iterations. This is the base-36 class: a 32-bit value is at most 7 digits there, so it
        ; always landed in the byte loop, and that loop was the whole remaining deficit against
        ; ucrtbase -- which reverses in place and never copies at all. Both moves stay strictly
        ; inside the digit run at both ends, so nothing outside [rdx, rdx+count) is written.
        cmp       rax, 4
        jae       s_wide
        ; 1..3 digits, BRANCH-FREE: first byte, last byte, middle byte. For count 1 all three
        ; target the same byte; for 2 they cover 0 and 1; for 3 they cover 0, 2 and 1. This
        ; replaces a counted loop whose per-iteration overhead dominated the two-digit case -- the
        ; smallest class in the bench, and the one where ucrtbase was still ahead.
        mov       r9b, byte ptr [rdi]
        mov       byte ptr [rdx], r9b
        mov       r9b, byte ptr [rdi + rax - 1]
        mov       byte ptr [rdx + rax - 1], r9b
        mov       r11, rax
        shr       r11, 1
        mov       r9b, byte ptr [rdi + r11]
        mov       byte ptr [rdx + r11], r9b
        add       rdx, rax
        jmp       s_done
s_wide:
        mov       r9d, dword ptr [rdi]
        mov       dword ptr [rdx], r9d
        mov       r9d, dword ptr [rdi + rax - 4]
        mov       dword ptr [rdx + rax - 4], r9d
        add       rdx, rax
        jmp       s_done
s_done:
        mov       byte ptr [rdx], 0
        xor       eax, eax
        jmp       epi

        ;================ the emit stopped early: the result cannot fit ================
capped:
        ; The bounded loop already wrote the reversed prefix into the caller's buffer as it went,
        ; and it stopped exactly at the cell limit, so the leftover ucrtbase would have produced is
        ; already in place. Only the terminator is missing. (`partial` below stays as the general
        ; path: it still serves the case where the bounded loop consumed the value on its last
        ; permitted digit, and it does not depend on maxdig being exact.)
        mov       byte ptr [rbx], 0
        jmp       e_range

        ;================ ERANGE: leave exactly ucrtbase's reversed leftovers ================
partial:
        mov       rax, rsi
        sub       rax, r12                     ; cells available after the sign
        cmp       rax, rcx
        cmova     rax, rcx                     ; k = min(available, digits)
        lea       rdx, [rbx + r12]             ; the sign cell is overwritten by the 0 below
        lea       r9, [r8 - 1]                 ; the least significant digit
p_cp:
        test      rax, rax
        jz        p_done
        movzx     r11d, byte ptr [r9]
        mov       byte ptr [rdx], r11b
        dec       r9
        inc       rdx
        dec       rax
        jmp       p_cp
p_done:
        mov       byte ptr [rbx], 0
        jmp       e_range

e_inval_none:
        mov       r12d, 22
        jmp       err_common
e_inval_wrote:
        mov       r12d, 22
        jmp       err_common
e_range:
        mov       r12d, 34
err_common:
        call      _errno                       ; ucrtbase's own, so the caller sees it where it
        mov       dword ptr [rax], r12d        ; expects; errno is set BEFORE the handler, as in
        call      _invalid_parameter_noinfo    ; the shipped code
        mov       eax, r12d
epi:
        add       rsp, 68h
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_itoa_s ENDP
END
